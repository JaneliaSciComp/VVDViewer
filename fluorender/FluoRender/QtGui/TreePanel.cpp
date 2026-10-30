#include "TreePanel.h"
#include <QPointer>

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QMenu>
#include <QToolBar>
#include <QAction>
#include <QPixmap>
#include <QSignalBlocker>
#include <QDrag>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QApplication>
#include <QFileDialog>
#include "FileDialogs.h"
#include <QMessageBox>
#include <QTimer>
#include <QScrollBar>
#include <QStringList>

#include <climits>

#include <boost/property_tree/ptree.hpp>
#include <boost/lexical_cast.hpp>

#include "DataManager.h"
#include "RenderCanvas.h"
#include "Formats/brkxml_reader.h"
#include "Formats/brkxml_writer.h"
#include "img/icons.h"
#include "img/tick.xpm"
#include "img/cross.xpm"

namespace
{
	enum { ROLE_TYPE = Qt::UserRole + 1, ROLE_PTR = Qt::UserRole + 2, ROLE_ID = Qt::UserRole + 3 };

	QIcon IconFromPng(const unsigned char* data, int len)
	{
		QPixmap pm;
		pm.loadFromData(data, (uint)len, "PNG");
		return QIcon(pm);
	}
#define VVD_ICON(name) IconFromPng(icons::name##_png, (int)sizeof(icons::name##_png))

	QColor ToQColor(const FLIVR::Color& c)
	{
		return QColor((unsigned char)(c.r() * 255), (unsigned char)(c.g() * 255), (unsigned char)(c.b() * 255));
	}
	QColor VolumeColor(VolumeData* vd) { return ToQColor(vd->GetColor()); }
	QColor MeshColor(MeshData* md)
	{
		FLIVR::Color amb, diff, spec;
		double shine, alpha;
		md->GetMaterial(amb, diff, spec, shine, alpha);
		return ToQColor(diff);
	}
	QString Q(const VVDString& s) { return QString::fromStdWString(s); }
	VVDString V(const QString& s) { return VVDString(s.toStdWString()); }
}

// ---------------------------------------------------------------------------
// DataTreeWidget: wx OnBeginDrag / OnEndDrag
// ---------------------------------------------------------------------------

DataTreeWidget::DataTreeWidget(TreePanel* panel, QWidget* parent)
	: QTreeWidget(parent), m_panel(panel)
{
}

// wx OnBeginDrag: volumes, meshes, annotations, groups and segments can be
// dragged; the drag carries nothing but the item
void DataTreeWidget::startDrag(Qt::DropActions)
{
	QTreeWidgetItem* item = currentItem();
	if (!item || m_panel->IsFixed())
		return;
	const int type = TreePanel::ItemType(item);
	if (type < TreePanel::TypeVolume || type > TreePanel::TypeSegGroup)
		return;
	m_drag_item = item;
	auto* drag = new QDrag(this);
	auto* mime = new QMimeData;
	mime->setData("application/x-vvd-tree-item", TreePanel::ItemName(item).toUtf8());
	drag->setMimeData(mime);
	const QIcon icon = item->icon(0);
	if (!icon.isNull()) drag->setPixmap(icon.pixmap(16, 16));
	drag->exec(Qt::MoveAction, Qt::MoveAction);
	m_drag_item = nullptr;
}

void DataTreeWidget::dragEnterEvent(QDragEnterEvent* e)
{
	if (e->mimeData()->hasFormat("application/x-vvd-tree-item") && m_drag_item)
		e->acceptProposedAction();
	else
		e->ignore();
}

void DataTreeWidget::dragMoveEvent(QDragMoveEvent* e)
{
	if (e->mimeData()->hasFormat("application/x-vvd-tree-item") && m_drag_item)
	{
		QTreeWidget::dragMoveEvent(e);   // auto-scroll, drop indicator
		e->acceptProposedAction();        // anywhere, incl. empty space (wx)
	}
	else
		e->ignore();
}

void DataTreeWidget::dropEvent(QDropEvent* e)
{
	QTreeWidgetItem* src = m_drag_item;
	if (!src || !e->mimeData()->hasFormat("application/x-vvd-tree-item"))
	{
		e->ignore();
		return;
	}
	const QPoint pos = e->position().toPoint();
	QTreeWidgetItem* dst = itemAt(pos);
	e->setDropAction(Qt::CopyAction);   // never let Qt remove the source
	e->accept();
	m_drag_item = nullptr;
	// changes the layer model; the tree is rebuilt after the drop returns
	m_panel->OnEndDrag(src, dst, pos.y());
}

// wx OnKeyDown: Delete / Backspace delete the selection; Enter toggles
void DataTreeWidget::keyPressEvent(QKeyEvent* e)
{
	if (state() != QAbstractItemView::EditingState &&
		(e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace))
	{
		m_panel->DeleteSelection();
		return;
	}
	QTreeWidget::keyPressEvent(e);
}

// ---------------------------------------------------------------------------
// TreePanel
// ---------------------------------------------------------------------------

TreePanel::TreePanel(QWidget* parent)
	: QWidget(parent)
{
	m_tree = new DataTreeWidget(this, this);
	m_tree->setColumnCount(1);
	m_tree->setHeaderLabel(tr("Active Datasets"));
	m_tree->header()->hide();
	m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
	m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
	m_tree->setIndentation(14);
	m_tree->setIconSize(QSize(16, 16));
	m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);   // wx: rename from the menu only
	m_tree->setExpandsOnDoubleClick(false);                       // wx: double-click = OnAct
	m_tree->setDragEnabled(true);
	m_tree->setAcceptDrops(true);
	m_tree->setDropIndicatorShown(true);
	m_tree->setDragDropMode(QAbstractItemView::DragDrop);
	m_tree->setDefaultDropAction(Qt::MoveAction);
	m_tree->setUniformRowHeights(true);

	// tool row, same order as the wx TreePanel toolbar
	auto* tb = new QToolBar(this);
	tb->setIconSize(QSize(20, 20));
	tb->setStyleSheet("QToolBar { border: none; spacing: 1px; padding: 1px; }");
	QAction* toggle = tb->addAction(VVD_ICON(listicon_toggle), tr("Toggle View"));
	toggle->setToolTip(tr("Toggle the visibility of current selection"));
	connect(toggle, &QAction::triggered, this, [this]() { OnAct(QApplication::keyboardModifiers() & Qt::ControlModifier); });
	m_save_act = tb->addAction(VVD_ICON(listicon_save), tr("Save As"));
	m_save_act->setToolTip(tr("Save: Save the selected volume dataset"));
	connect(m_save_act, &QAction::triggered, this, &TreePanel::OnSave);
	m_bake_act = tb->addAction(VVD_ICON(listicon_bake), tr("Bake"));
	m_bake_act->setToolTip(tr("Bake: Apply the volume properties and save"));
	connect(m_bake_act, &QAction::triggered, this, &TreePanel::OnBakeVolume);
	m_save_act->setEnabled(false);
	m_bake_act->setEnabled(false);
	QAction* del = tb->addAction(VVD_ICON(listicon_delete), tr("Delete"));
	del->setToolTip(tr("Delete current selection"));
	connect(del, &QAction::triggered, this, &TreePanel::DeleteSelection);
	tb->addSeparator();
	// brush tools, same as the wx TreePanel check tools
	m_brush_append = tb->addAction(VVD_ICON(listicon_brushappend), tr("Highlight"));
	m_brush_append->setCheckable(true);
	m_brush_append->setToolTip(tr("Highlight structures by painting on the render view (hold Shift)"));
	m_brush_diffuse = tb->addAction(VVD_ICON(listicon_brushdiffuse), tr("Diffuse"));
	m_brush_diffuse->setCheckable(true);
	m_brush_diffuse->setToolTip(tr("Diffuse highlighted structures by painting (hold Z)"));
	m_brush_desel = tb->addAction(VVD_ICON(listicon_brushdesel), tr("Reset"));
	m_brush_desel->setCheckable(true);
	m_brush_desel->setToolTip(tr("Reset highlighted structures by painting (hold X)"));
	QAction* brush_clear = tb->addAction(VVD_ICON(listicon_brushclear), tr("Reset All"));
	brush_clear->setToolTip(tr("Reset all highlighted structures"));
	tb->addSeparator();
	QAction* brush_erase = tb->addAction(VVD_ICON(listicon_brusherase), tr("Erase"));
	brush_erase->setToolTip(tr("Erase highlighted structures"));
	QAction* brush_create = tb->addAction(VVD_ICON(listicon_brushcreate), tr("Extract"));
	brush_create->setToolTip(tr("Extract highlighted structures out and create a new volume"));
	connect(m_brush_append, &QAction::triggered, this, [this](bool on) { OnBrushToggle(m_brush_append, 2, on); });
	connect(m_brush_diffuse, &QAction::triggered, this, [this](bool on) { OnBrushToggle(m_brush_diffuse, 4, on); });
	connect(m_brush_desel, &QAction::triggered, this, [this](bool on) { OnBrushToggle(m_brush_desel, 3, on); });
	connect(brush_clear, &QAction::triggered, this, [this]() { Q_EMIT brushClearRequested(); });
	connect(brush_erase, &QAction::triggered, this, [this]() { SelectBrush(0); Q_EMIT brushEraseRequested(); });
	connect(brush_create, &QAction::triggered, this, [this]() { SelectBrush(0); Q_EMIT brushExtractRequested(); });

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addWidget(tb);
	layout->addWidget(m_tree);

	connect(m_tree, &QTreeWidget::itemChanged, this, &TreePanel::OnItemChanged);
	connect(m_tree, &QTreeWidget::currentItemChanged, this, &TreePanel::OnCurrentItemChanged);
	connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &TreePanel::OnContextMenu);
	// wx OnExpandItem -> ExpandROITree (SWC segment items are built on demand)
	connect(m_tree, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* item) { ExpandROITree(item); });
	// wx EVT_TREE_ITEM_ACTIVATED -> OnAct (double-click / Enter)
	connect(m_tree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item, int) {
		if (item && item == m_tree->currentItem())
			OnAct(QApplication::keyboardModifiers() & Qt::ControlModifier);
	});
	// rename: an editor closed without a change restores the flags
	connect(m_tree->itemDelegate(), &QAbstractItemDelegate::closeEditor, this, [this]() {
		if (m_rename_item)
		{
			QTreeWidgetItem* it = m_rename_item;
			m_rename_item = nullptr;
			QSignalBlocker b(m_tree);
			it->setFlags(it->flags() & ~Qt::ItemIsEditable);
			if (it->text(0).isEmpty()) it->setText(0, m_rename_prev);
		}
	});

	UpdateTree();
}

// ---------------------------------------------------------------------------
// item helpers
// ---------------------------------------------------------------------------

int TreePanel::ItemType(const QTreeWidgetItem* item)
{
	if (!item) return -1;
	QVariant v = item->data(0, ROLE_TYPE);
	return v.isValid() ? v.toInt() : -1;
}

QString TreePanel::ItemName(const QTreeWidgetItem* item)
{
	return item ? item->text(0) : QString();
}

VolumeData* TreePanel::VolumeOf(const QTreeWidgetItem* item) const
{
	if (ItemType(item) != TypeVolume) return nullptr;
	return reinterpret_cast<VolumeData*>(item->data(0, ROLE_PTR).value<quintptr>());
}

MeshData* TreePanel::MeshOf(const QTreeWidgetItem* item) const
{
	if (ItemType(item) != TypeMesh) return nullptr;
	return reinterpret_cast<MeshData*>(item->data(0, ROLE_PTR).value<quintptr>());
}

// wx GetParentVolItem: the volume item a segment item belongs to
QTreeWidgetItem* TreePanel::ParentVolItem(QTreeWidgetItem* item) const
{
	QTreeWidgetItem* p = item;
	while (p)
	{
		if (ItemType(p) == TypeVolume) return p;
		p = p->parent();
	}
	return nullptr;
}

// wx GetParentMeshItem: the mesh item an SWC segment item belongs to
QTreeWidgetItem* TreePanel::ParentMeshItem(QTreeWidgetItem* item) const
{
	const int type = ItemType(item);
	if (type != TypeMeshSeg && type != TypeMeshSegGroup) return nullptr;
	for (QTreeWidgetItem* p = item->parent(); p; p = p->parent())
		if (ItemType(p) == TypeMesh) return p;
	return nullptr;
}

// wx DataTreeCtrl::ChangeIconColor: the cross / tick light bulb XPMs with
// their transparent ("None") colour replaced by the data colour
QIcon TreePanel::BulbIcon(const QColor& c, bool on) const
{
	const char** orgn = on ? tick_xpm : cross_xpm;
	int w = 0, h = 0, ncolors = 0, cpp = 0;
	sscanf(orgn[0], "%d %d %d %d", &w, &h, &ncolors, &cpp);
	const int lines = 1 + ncolors + h;
	std::vector<QByteArray> data;
	data.reserve(lines);
	for (int i = 0; i < lines; i++)
	{
		QByteArray line(orgn[i]);
		if (i == 1)   // wx icon_change: the line of icon_key ("None")
			line.replace("None", c.name(QColor::HexRgb).toUpper().toLatin1());
		data.push_back(line);
	}
	std::vector<const char*> ptrs;
	for (const QByteArray& b : data) ptrs.push_back(b.constData());
	QPixmap pm(ptrs.data());
	return QIcon(pm);
}

void TreePanel::SetItemBulb(QTreeWidgetItem* item, const QColor& c, bool on)
{
	if (!item) return;
	const bool was = m_updating;
	m_updating = true;
	item->setIcon(0, BulbIcon(c, on));
	m_updating = was;
}

QTreeWidgetItem* TreePanel::AddItem(QTreeWidgetItem* parent, const QString& text, int type, void* ptr, int id)
{
	auto* item = parent ? new QTreeWidgetItem(parent, QStringList(text)) : new QTreeWidgetItem(m_tree, QStringList(text));
	Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDropEnabled;
	if (type >= TypeVolume && type <= TypeSegGroup)
		f |= Qt::ItemIsDragEnabled;
	item->setFlags(f);
	item->setData(0, ROLE_TYPE, type);
	item->setData(0, ROLE_PTR, QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(ptr)));
	item->setData(0, ROLE_ID, id);
	return item;
}

// wx DataTreeCtrl::BuildROITree (segments of a colormap-index volume)
void TreePanel::BuildROITree(QTreeWidgetItem* par_item, const void* tree_ptr, VolumeData* vd)
{
	using boost::property_tree::wptree;
	const wptree& tree = *(const wptree*)tree_ptr;
	for (wptree::const_iterator child = tree.begin(); child != tree.end(); ++child)
	{
		if (const auto val = tree.get_optional<std::wstring>(child->first))
		{
			try
			{
				const wptree& subtree = child->second;
				int id = boost::lexical_cast<int>(child->first);
				if (id <= 0 || !vd || vd->IsROICombined(id) < 0)
				{
					const int type = (id > 0) ? TypeSegment : TypeSegGroup;
					QTreeWidgetItem* item = AddItem(par_item, QString::fromStdWString(*val), type, vd, id);
					unsigned char r = 255, g = 255, b = 255;
					if (type == TypeSegment && vd)
						vd->GetIDColor(r, g, b, id);
					SetItemBulb(item, QColor(r, g, b), vd && vd->isSelID(id));
					BuildROITree(item, &subtree, vd);
				}
			}
			catch (const boost::bad_lexical_cast&)
			{
			}
		}
	}
}

// wx DataTreeCtrl::InitROITree (MeshData): a "dummy" child when the SWC
// segment tree is not empty; the items are made when it is expanded
void TreePanel::InitMeshROITree(QTreeWidgetItem* par_item, MeshData* md)
{
	using boost::property_tree::wptree;
	if (!par_item || !md || !md->isTree() || !md->getROITree()) return;
	const wptree& tree = *md->getROITree();
	for (wptree::const_iterator child = tree.begin(); child != tree.end(); ++child)
		if (tree.get_optional<std::wstring>(child->first))
		{
			auto* dummy = new QTreeWidgetItem(par_item, QStringList("dummy"));
			dummy->setFlags(Qt::ItemIsEnabled);
			return;
		}
}

// wx DataTreeCtrl::AddChildrenROITree (MeshData): one level of the SWC
// segment tree (key = name, value = id; id >= 0 segment, < 0 group)
void TreePanel::AddChildrenROITree(QTreeWidgetItem* par_item, const void* tree_ptr, MeshData* md)
{
	using boost::property_tree::wptree;
	if (!par_item || !tree_ptr) return;
	const bool was = m_updating;
	m_updating = true;
	qDeleteAll(par_item->takeChildren());
	const wptree& tree = *(const wptree*)tree_ptr;
	for (wptree::const_iterator child = tree.begin(); child != tree.end(); ++child)
	{
		if (const auto val = tree.get_optional<std::wstring>(child->first))
		{
			try
			{
				const int id = boost::lexical_cast<int>(*val);
				const int type = id >= 0 ? TypeMeshSeg : TypeMeshSegGroup;
				QTreeWidgetItem* item = AddItem(par_item, QString::fromStdWString(child->first), type, md, id);
				unsigned char r = 255, g = 255, b = 255;
				if (type == TypeMeshSeg && md) md->GetIDColor(r, g, b, id);
				SetItemBulb(item, QColor(r, g, b), md && md->GetROIState(id));
				const wptree& subtree = child->second;
				if (subtree.begin() != subtree.end())
				{
					auto* dummy = new QTreeWidgetItem(item, QStringList("dummy"));
					dummy->setFlags(Qt::ItemIsEnabled);
				}
			}
			catch (const boost::bad_lexical_cast&)
			{
			}
		}
	}
	m_updating = was;
}

// wx DataTreeCtrl::ExpandROITree: the children of a tree mesh / SWC segment
// item the first time it is expanded (it only holds the dummy)
void TreePanel::ExpandROITree(QTreeWidgetItem* item)
{
	if (!item || item->childCount() != 1 || ItemType(item->child(0)) != -1) return;
	const int type = ItemType(item);
	if (type == TypeMesh)
	{
		MeshData* md = MeshOf(item);
		if (md && md->isTree() && md->getROITree())
			AddChildrenROITree(item, md->getROITree(), md);
	}
	else if (type == TypeMeshSeg || type == TypeMeshSegGroup)
	{
		MeshData* md = MeshOf(ParentMeshItem(item));
		if (md)
			if (auto* child = md->getROITree(item->data(0, ROLE_ID).toInt()))
				AddChildrenROITree(item, child, md);
	}
}

// wx DataTreeCtrl::FindTreeItemBySegmentID (loaded items only)
QTreeWidgetItem* TreePanel::FindItemBySegmentID(QTreeWidgetItem* par_item, int id) const
{
	if (!par_item) return nullptr;
	const int type = ItemType(par_item);
	if ((type == TypeSegment || type == TypeSegGroup || type == TypeMeshSeg || type == TypeMeshSegGroup) &&
		par_item->data(0, ROLE_ID).toInt() == id)
		return par_item;
	for (int i = 0; i < par_item->childCount(); i++)
		if (QTreeWidgetItem* r = FindItemBySegmentID(par_item->child(i), id))
			return r;
	return nullptr;
}

// ---------------------------------------------------------------------------
// wx VRenderFrame::UpdateTree / UpdateTreeIcons / UpdateTreeColors
// ---------------------------------------------------------------------------

void TreePanel::UpdateTree(const QString& name_in, int type)
{
	QString name = name_in;
	int sel_type = type;
	QTreeWidgetItem* cur = m_tree->currentItem();
	if (name.isEmpty() && cur)
	{
		// keep the selection (wx callers pass the selected name)
		name = ItemName(cur);
		sel_type = ItemType(cur);
	}
	if (m_tree->topLevelItemCount() > 0)
		SaveExpState();
	const int scroll = m_tree->verticalScrollBar() ? m_tree->verticalScrollBar()->value() : 0;

	m_updating = true;
	m_rename_item = nullptr;
	m_tree->clear();

	QTreeWidgetItem* root = AddItem(nullptr, tr("Active Datasets"), TypeRoot, nullptr);
	QTreeWidgetItem* sel_item = nullptr;
	if (name == root->text(0) && (sel_type == TypeRoot || sel_type < 0)) sel_item = root;
	bool expand_newitem = true;

	for (RenderCanvas* m_canvas : m_canvases)
	{
		if (!m_canvas) continue;
		m_canvas->OrganizeLayers();
		QTreeWidgetItem* vrv_item = AddItem(root, m_canvas->GetViewName(), TypeView, m_canvas);
		SetItemBulb(vrv_item, Qt::white, m_canvas->GetDraw());
		if (name == vrv_item->text(0) && (sel_type == TypeView || sel_type < 0)) sel_item = vrv_item;
		auto match = [&](const QString& n, int t) { return n == name && (sel_type == t || sel_type < 0); };

		for (int j = 0; j < m_canvas->GetLayerNum(); j++)
		{
			TreeLayer* layer = m_canvas->GetLayer(j);
			if (!layer) continue;
			switch (layer->IsA())
			{
			case 2://volume data
			{
				VolumeData* vd = (VolumeData*)layer;
				QTreeWidgetItem* item = AddItem(vrv_item, Q(vd->GetName()), TypeVolume, vd);
				SetItemBulb(item, VolumeColor(vd), vd->GetDisp());
				if (vd->GetColormapMode() == 3 && vd->getROITree())
					BuildROITree(item, vd->getROITree(), vd);
				if (match(item->text(0), TypeVolume)) sel_item = item;
			}
			break;
			case 3://mesh data
			{
				MeshData* md = (MeshData*)layer;
				QTreeWidgetItem* item = AddItem(vrv_item, Q(md->GetName()), TypeMesh, md);
				expand_newitem = false;
				SetItemBulb(item, MeshColor(md), md->GetDisp());
				InitMeshROITree(item, md);   // wx AddMeshItem
				if (match(item->text(0), TypeMesh)) sel_item = item;
			}
			break;
			case 4://annotations
			{
				Annotations* ann = (Annotations*)layer;
				QTreeWidgetItem* item = AddItem(vrv_item, Q(ann->GetName()), TypeAnnotations, ann);
				SetItemBulb(item, Qt::white, ann->GetDisp());
				if (match(item->text(0), TypeAnnotations)) sel_item = item;
			}
			break;
			case 5://group
			{
				DataGroup* group = (DataGroup*)layer;
				QTreeWidgetItem* group_item = AddItem(vrv_item, Q(group->GetName()), TypeGroup, group);
				SetItemBulb(group_item, Qt::white, group->GetDisp());
				for (int k = 0; k < group->GetVolumeNum(); k++)
				{
					VolumeData* vd = group->GetVolumeData(k);
					if (!vd) continue;
					QTreeWidgetItem* item = AddItem(group_item, Q(vd->GetName()), TypeVolume, vd);
					SetItemBulb(item, VolumeColor(vd), vd->GetDisp());
					if (vd->GetColormapMode() == 3 && vd->getROITree())
						BuildROITree(item, vd->getROITree(), vd);
					if (match(item->text(0), TypeVolume)) sel_item = item;
				}
				if (match(group_item->text(0), TypeGroup)) sel_item = group_item;
			}
			break;
			case 6://mesh group
			{
				MeshGroup* group = (MeshGroup*)layer;
				QTreeWidgetItem* group_item = AddItem(vrv_item, Q(group->GetName()), TypeMGroup, group);
				SetItemBulb(group_item, Qt::white, group->GetDisp());
				for (int k = 0; k < group->GetMeshNum(); k++)
				{
					MeshData* md = group->GetMeshData(k);
					if (!md) continue;
					QTreeWidgetItem* item = AddItem(group_item, Q(md->GetName()), TypeMesh, md);
					expand_newitem = false;
					SetItemBulb(item, MeshColor(md), md->GetDisp());
					InitMeshROITree(item, md);   // wx AddMeshItem
					if (match(item->text(0), TypeMesh)) sel_item = item;
				}
				if (match(group_item->text(0), TypeMGroup)) sel_item = group_item;
			}
			break;
			}
		}
	}

	// wx LoadExpState(expand_newitem); the root is always expanded
	LoadExpState(root, QString(), expand_newitem);
	root->setExpanded(true);
	m_updating = false;
	if (sel_item)
	{
		m_tree->setCurrentItem(sel_item);
		if (m_tree->currentItem() == sel_item && cur == nullptr)
			; // already reported by currentItemChanged
	}
	else
		UpdateSelection();   // nothing selected any more (wx: OnSelection of the null item)
	// Keep the list where it was (delete, toggle, rename, ... rebuild the
	// whole tree). The scroll range is only known after the rows are laid
	// out, and selecting an item may scroll on its own, so restore the
	// position after both -- now and once more after the pending layout.
	RestoreScroll(scroll);
	UpdateToolbar();
	// wx UpdateTree end: m_plugin_manager->OnTreeUpdate()
	Q_EMIT treeUpdated();
}

void TreePanel::RestoreScroll(int value)
{
	QScrollBar* vsb = m_tree->verticalScrollBar();
	if (!vsb) return;
	m_tree->doItemsLayout();
	vsb->setValue(value);
	QPointer<QScrollBar> guard(vsb);
	QTimer::singleShot(0, this, [guard, value]() { if (guard) guard->setValue(value); });
}

// wx SaveExpState: "<prefix><name>#<type>" -> expanded
void TreePanel::SaveExpState()
{
	m_exp_state.clear();
	for (int i = 0; i < m_tree->topLevelItemCount(); i++)
		SaveExpState(m_tree->topLevelItem(i), QString());
	m_exp_loaded = true;
}

void TreePanel::SaveExpState(QTreeWidgetItem* item, const QString& prefix)
{
	if (!item) return;
	const int type = ItemType(item);
	m_exp_state[prefix + item->text(0) + QString("#%1").arg(type)] = item->isExpanded();
	QString child_prefix = prefix;
	if (type == TypeVolume || type == TypeMesh)
		child_prefix = item->text(0) + ".";
	for (int i = 0; i < item->childCount(); i++)
		if (ItemType(item->child(i)) >= 0)
			SaveExpState(item->child(i), child_prefix);
}

// wx TraversalExpand: expand the item's ancestors
void TreePanel::TraversalExpand(QTreeWidgetItem* item)
{
	if (!item) return;
	QTreeWidgetItem* parent = item->parent();
	while (parent)
	{
		parent->setExpanded(true);
		parent = parent->parent();
	}
}

// wx LoadExpState: known items get their state back, new ones are shown
// (their ancestors expanded) unless expand_newitem is off
void TreePanel::LoadExpState(QTreeWidgetItem* item, const QString& prefix, bool expand_newitem)
{
	if (!item) return;
	const int type = ItemType(item);
	bool is_new = false;
	const QString name = prefix + item->text(0) + QString("#%1").arg(type);
	auto it = m_exp_state.find(name);
	if (it != m_exp_state.end())
		item->setExpanded(it->second);
	else
	{
		is_new = true;
		if (expand_newitem)
			TraversalExpand(item);
		else
			item->setExpanded(false);
	}
	bool expand_newitem_child = expand_newitem;
	QString child_prefix = prefix;
	if (type == TypeVolume || type == TypeMesh)
	{
		child_prefix = item->text(0) + ".";
		if (is_new)
			expand_newitem_child = false;
	}
	for (int i = 0; i < item->childCount(); i++)
		if (ItemType(item->child(i)) >= 0)
			LoadExpState(item->child(i), child_prefix, expand_newitem_child);
}

// wx ExportExpState / ImportExpState ("key\n1|0\n" ...)
QString TreePanel::ExportExpState()
{
	SaveExpState();
	QString rval;
	for (const auto& kv : m_exp_state)
		rval += kv.first + "\n" + (kv.second ? "1" : "0") + "\n";
	return rval;
}

void TreePanel::ImportExpState(const QString& state)
{
	m_exp_state.clear();
	const QStringList lines = state.split('\n');
	for (int i = 0; i + 1 < lines.size(); i += 2)
	{
		if (lines[i + 1] == "1") m_exp_state[lines[i]] = true;
		else if (lines[i + 1] == "0") m_exp_state[lines[i]] = false;
	}
	m_updating = true;
	for (int i = 0; i < m_tree->topLevelItemCount(); i++)
		LoadExpState(m_tree->topLevelItem(i), QString(), true);
	m_updating = false;
}

void TreePanel::UpdateTreeIcons()
{
	std::function<void(QTreeWidgetItem*)> walk = [&](QTreeWidgetItem* item) {
		const int type = ItemType(item);
		void* ptr = reinterpret_cast<void*>(item->data(0, ROLE_PTR).value<quintptr>());
		switch (type)
		{
		case TypeView: if (ptr) SetItemBulb(item, Qt::white, ((RenderCanvas*)ptr)->GetDraw()); break;
		case TypeVolume: SetItemBulb(item, VolumeColor((VolumeData*)ptr), ((VolumeData*)ptr)->GetDisp()); break;
		case TypeMesh: SetItemBulb(item, MeshColor((MeshData*)ptr), ((MeshData*)ptr)->GetDisp()); break;
		case TypeAnnotations: SetItemBulb(item, Qt::white, ((Annotations*)ptr)->GetDisp()); break;
		case TypeGroup: SetItemBulb(item, Qt::white, ((DataGroup*)ptr)->GetDisp()); break;
		case TypeMGroup: SetItemBulb(item, Qt::white, ((MeshGroup*)ptr)->GetDisp()); break;
		case TypeSegment:
		case TypeSegGroup:
		{
			VolumeData* vd = (VolumeData*)ptr;
			const int id = item->data(0, ROLE_ID).toInt();
			unsigned char r = 255, g = 255, b = 255;
			if (type == TypeSegment && vd) vd->GetIDColor(r, g, b, id);
			SetItemBulb(item, QColor(r, g, b), vd && vd->isSelID(id));
		}
		break;
		case TypeMeshSeg:
		case TypeMeshSegGroup:
		{
			// wx UpdateROITreeIcons / UpdateROITreeIconColor (MeshData)
			MeshData* md = (MeshData*)ptr;
			const int id = item->data(0, ROLE_ID).toInt();
			unsigned char r = 255, g = 255, b = 255;
			if (type == TypeMeshSeg && md) md->GetIDColor(r, g, b, id);
			SetItemBulb(item, QColor(r, g, b), md && md->GetROIState(id));
		}
		break;
		}
		for (int i = 0; i < item->childCount(); i++)
			walk(item->child(i));
	};
	for (int i = 0; i < m_tree->topLevelItemCount(); i++)
		walk(m_tree->topLevelItem(i));
	// wx UpdateTreeIcons / UpdateTreeColors end: m_plugin_manager->OnTreeUpdate()
	Q_EMIT treeUpdated();
}

// the bulbs carry both the visibility and the colour
void TreePanel::UpdateTreeColors()
{
	UpdateTreeIcons();
}

void TreePanel::RefreshViews()
{
	Q_EMIT refreshRequested();
}

// the view item an item hangs under (wx: GetItemParent up to type 1)
RenderCanvas* TreePanel::CanvasOf(const QTreeWidgetItem* item) const
{
	for (const QTreeWidgetItem* p = item; p; p = p->parent())
		if (ItemType(p) == TypeView)
		{
			RenderCanvas* c = reinterpret_cast<RenderCanvas*>(p->data(0, ROLE_PTR).value<quintptr>());
			for (RenderCanvas* k : m_canvases) if (k == c) return c;
			break;
		}
	return m_canvases.empty() ? nullptr : m_canvases[0];
}

RenderCanvas* TreePanel::CurCanvas() const
{
	return CanvasOf(m_tree ? m_tree->currentItem() : nullptr);
}

bool TreePanel::SelectView(RenderCanvas* canvas)
{
	if (!canvas) return false;
	QTreeWidgetItem* root = m_tree->topLevelItemCount() ? m_tree->topLevelItem(0) : nullptr;
	for (int i = 0; root && i < root->childCount(); i++)
	{
		QTreeWidgetItem* it = root->child(i);
		if (ItemType(it) == TypeView && reinterpret_cast<RenderCanvas*>(it->data(0, ROLE_PTR).value<quintptr>()) == canvas)
		{
			m_tree->setCurrentItem(it);
			return true;
		}
	}
	return false;
}

// wx DataTreeCtrl::OnCloseView
void TreePanel::OnCloseView()
{
	if (m_fixed) return;
	QTreeWidgetItem* sel_item = m_tree->currentItem();
	if (!sel_item || ItemType(sel_item) != TypeView) return;
	RenderCanvas* c = CanvasOf(sel_item);
	if (!c || (!m_canvases.empty() && c == m_canvases[0])) return;
	Q_EMIT closeViewRequested(c);
}

// Qt extension (wx keeps every drag inside one view): a volume or mesh
// dropped onto an item of another view leaves its view (and group) and is
// added to that view -- into the group it was dropped on / into, else at
// the view level (a volume then gets a group through OrganizeLayers)
void TreePanel::MoveToView(QTreeWidgetItem* src_item, QTreeWidgetItem* dst_item)
{
	RenderCanvas* src = CanvasOf(src_item);
	RenderCanvas* dst = CanvasOf(dst_item);
	if (!src || !dst || src == dst) return;
	const int type = ItemType(src_item);
	const QString name = src_item->text(0);
	// the destination group: the dropped-on group, or the group of the
	// dropped-on item
	QTreeWidgetItem* gitem = nullptr;
	for (QTreeWidgetItem* p = dst_item; p; p = p->parent())
	{
		const int t = ItemType(p);
		if ((type == TypeVolume && t == TypeGroup) || (type == TypeMesh && t == TypeMGroup)) { gitem = p; break; }
		if (t == TypeView) break;
	}
	if (type == TypeVolume)
	{
		VolumeData* vd = VolumeOf(src_item);
		if (!vd) return;
		src->RemoveVolumeData(V(name));
		if (src->GetCurrentVolume() == vd) src->SetCurrentVolume(nullptr);
		if (gitem)
			dst->AddVolumeData(vd, V(gitem->text(0)));
		else
			dst->AddVolumeData(vd);
	}
	else if (type == TypeMesh)
	{
		MeshData* md = MeshOf(src_item);
		if (!md) return;
		if (src->GetCurrentMesh() == md) src->SetCurrentMesh(nullptr);
		src->RemoveMeshData(V(name));
		MeshGroup* mg = gitem ? dst->GetMGroup(V(gitem->text(0))) : nullptr;
		if (mg)
		{
			mg->InsertMeshData(mg->GetMeshNum() - 1, md);
			dst->LayersModified();
		}
		else
			dst->AddMeshData(md);
	}
	else
		return;
	QTimer::singleShot(0, this, [this, name, type]() {
		UpdateTree(name, type);
		RefreshViews();
	});
}

// ---------------------------------------------------------------------------
// selection
// ---------------------------------------------------------------------------

QTreeWidgetItem* TreePanel::FindItem(const QString& name, int type) const
{
	QTreeWidgetItemIterator it(m_tree);
	while (*it)
	{
		if ((*it)->text(0) == name && (type < 0 || ItemType(*it) == type))
			return *it;
		++it;
	}
	return nullptr;
}

bool TreePanel::SelectByName(const QString& name, int type)
{
	if (QTreeWidgetItem* item = FindItem(name, type))
	{
		m_tree->setCurrentItem(item);
		return true;
	}
	return false;
}

void TreePanel::SelectVolume(VolumeData* vd)
{
	QTreeWidgetItemIterator it(m_tree);
	while (*it)
	{
		if (VolumeOf(*it) == vd && vd) { m_tree->setCurrentItem(*it); return; }
		++it;
	}
}

void TreePanel::SelectVolume(VolumeData* vd, RenderCanvas* canvas)
{
	if (!canvas) { SelectVolume(vd); return; }
	QTreeWidgetItemIterator it(m_tree);
	while (*it)
	{
		if (VolumeOf(*it) == vd && vd && CanvasOf(*it) == canvas) { m_tree->setCurrentItem(*it); return; }
		++it;
	}
	SelectVolume(vd);
}

VolumeData* TreePanel::CurrentVolume() const
{
	return VolumeOf(m_tree->currentItem());
}

void TreePanel::SelectMesh(MeshData* md)
{
	QTreeWidgetItemIterator it(m_tree);
	while (*it)
	{
		if (MeshOf(*it) == md && md) { m_tree->setCurrentItem(*it); return; }
		++it;
	}
}

bool TreePanel::SelectAnnotations(Annotations* ann)
{
	QTreeWidgetItemIterator it(m_tree);
	while (*it)
	{
		if (ann && ItemType(*it) == TypeAnnotations &&
			reinterpret_cast<Annotations*>((*it)->data(0, ROLE_PTR).value<quintptr>()) == ann)
		{
			m_tree->setCurrentItem(*it);
			return true;
		}
		++it;
	}
	return false;
}

MeshData* TreePanel::CurrentMesh() const
{
	return MeshOf(m_tree->currentItem());
}

QString TreePanel::GetCurrentSel() const
{
	return ItemName(m_tree->currentItem());
}

int TreePanel::GetCurrentSelType() const
{
	return ItemType(m_tree->currentItem());
}

void TreePanel::OnCurrentItemChanged(QTreeWidgetItem*, QTreeWidgetItem*)
{
	if (m_updating) return;
	UpdateSelection();
	UpdateToolbar();
}

// wx TreePanel::doAction: Save / Bake only for a volume
void TreePanel::UpdateToolbar()
{
	const bool vol = GetCurrentSelType() == TypeVolume;
	if (m_save_act) m_save_act->setEnabled(vol);
	if (m_bake_act) m_bake_act->setEnabled(vol);
}

// wx DataTreeCtrl::UpdateSelection
void TreePanel::UpdateSelection()
{
	QTreeWidgetItem* sel_item = m_tree->currentItem();
	if (!sel_item)
	{
		Q_EMIT selectionChanged(-1, nullptr, nullptr, nullptr);
		return;
	}
	switch (ItemType(sel_item))
	{
	case TypeRoot:
		Q_EMIT selectionChanged(0, nullptr, nullptr, nullptr);
		break;
	case TypeView:
		Q_EMIT selectionChanged(1, nullptr, nullptr, nullptr);
		break;
	case TypeVolume:
	{
		VolumeData* vd = VolumeOf(sel_item);
		QTreeWidgetItem* par = sel_item->parent();
		DataGroup* group = nullptr;
		if (ItemType(par) == TypeGroup)
			group = reinterpret_cast<DataGroup*>(par->data(0, ROLE_PTR).value<quintptr>());
		Q_EMIT selectionChanged(2, group, vd, nullptr);
	}
	break;
	case TypeMesh:
	{
		MeshData* md = MeshOf(sel_item);
		if (md && md->isTree())
			md->SelectSegment();
		Q_EMIT selectionChanged(3, nullptr, nullptr, md);
	}
	break;
	case TypeMeshSeg:
	case TypeMeshSegGroup:
	{
		// wx: md->SelectSegment(id); OnSelection(3, .., md)
		MeshData* md = MeshOf(ParentMeshItem(sel_item));
		if (!md) break;
		md->SelectSegment(sel_item->data(0, ROLE_ID).toInt());
		Q_EMIT selectionChanged(3, nullptr, nullptr, md);
	}
	break;
	case TypeAnnotations:
		// wx: OnSelection(4, vrv, 0, 0, 0, ann) -- the annotations are read
		// back with SelectedAnnotations()
		m_sel_ann = reinterpret_cast<Annotations*>(sel_item->data(0, ROLE_PTR).value<quintptr>());
		Q_EMIT selectionChanged(4, nullptr, nullptr, nullptr);
		m_sel_ann = nullptr;
		break;
	case TypeGroup:
		Q_EMIT selectionChanged(5, reinterpret_cast<DataGroup*>(sel_item->data(0, ROLE_PTR).value<quintptr>()), nullptr, nullptr);
		break;
	case TypeMGroup:
		Q_EMIT selectionChanged(6, nullptr, nullptr, nullptr);
		break;
	case TypeSegment:
	case TypeSegGroup:
	{
		QTreeWidgetItem* vitem = ParentVolItem(sel_item);
		VolumeData* vd = VolumeOf(vitem);
		if (!vd) break;
		int id = vd->GetROIid(sel_item->text(0).toStdWString());
		vd->SetEditSelID(id);
		QTreeWidgetItem* par = vitem->parent();
		DataGroup* group = nullptr;
		if (ItemType(par) == TypeGroup)
			group = reinterpret_cast<DataGroup*>(par->data(0, ROLE_PTR).value<quintptr>());
		Q_EMIT selectionChanged(2, group, vd, nullptr);
	}
	break;
	}
}

// wx DataTreeCtrl::SelectROI (volume)
void TreePanel::SelectROI(VolumeData* vd, int id)
{
	if (!vd) return;
	const QString rname = QString::fromStdWString(vd->GetROIName(id));
	QTreeWidgetItem* vitem = nullptr;
	QTreeWidgetItemIterator it(m_tree);
	while (*it) { if (VolumeOf(*it) == vd) { vitem = *it; break; } ++it; }
	if (!vitem) return;
	QTreeWidgetItem* ritem = nullptr;
	if (!rname.isEmpty())
	{
		QTreeWidgetItemIterator rit(vitem);
		++rit;
		while (*rit && ParentVolItem(*rit) == vitem)
		{
			if ((*rit)->text(0) == rname) { ritem = *rit; break; }
			++rit;
		}
	}
	if (ritem)
	{
		TraversalExpand(ritem);
		m_tree->setCurrentItem(ritem);
	}
	else
		m_tree->setCurrentItem(vitem);
}

// ---------------------------------------------------------------------------
// brush buttons
// ---------------------------------------------------------------------------

// wx TreePanel::BrushAppend / BrushDiffuse / BrushDesel: the other two
// check tools go up, the view enters paint mode (down) or view mode (up);
// the tree is fixed while painting
void TreePanel::OnBrushToggle(QAction* act, int paint_mode, bool checked)
{
	for (QAction* a : { m_brush_append, m_brush_diffuse, m_brush_desel })
	{
		if (a == act) continue;
		QSignalBlocker b(a);
		a->setChecked(false);
	}
	m_fixed = checked;
	Q_EMIT brushModeRequested(checked ? paint_mode : 0);
}

void TreePanel::SelectBrush(int paint_mode)
{
	if (!m_brush_append) return;
	QSignalBlocker b1(m_brush_append);
	QSignalBlocker b2(m_brush_diffuse);
	QSignalBlocker b3(m_brush_desel);
	m_brush_append->setChecked(paint_mode == 2);
	m_brush_diffuse->setChecked(paint_mode == 4);
	m_brush_desel->setChecked(paint_mode == 3);
	m_fixed = paint_mode == 2 || paint_mode == 4 || paint_mode == 3;
}

int TreePanel::GetBrushSelected() const
{
	if (m_brush_append && m_brush_append->isChecked()) return 2;
	if (m_brush_diffuse && m_brush_diffuse->isChecked()) return 4;
	if (m_brush_desel && m_brush_desel->isChecked()) return 3;
	return 0;
}

// ---------------------------------------------------------------------------
// wx OnAct: toggle visibility (Ctrl: randomize colours)
// ---------------------------------------------------------------------------

void TreePanel::OnAct(bool rc)
{
	if (m_fixed || !CurCanvas()) return;
	QTreeWidgetItem* sel_item = m_tree->currentItem();
	if (!sel_item) return;
	const QString name = sel_item->text(0);
	const int type = ItemType(sel_item);
	if (type < 0) return;
	void* ptr = reinterpret_cast<void*>(sel_item->data(0, ROLE_PTR).value<quintptr>());
	PushVisHistory();   // wx OnAct
	switch (type)
	{
	case TypeView:
		if (rc) CurCanvas()->RandomizeColor();
		else CurCanvas()->ToggleDraw();
		break;
	case TypeVolume:
	{
		VolumeData* vd = (VolumeData*)ptr;
		if (rc) vd->RandomizeColor();
		else vd->ToggleDisp();
	}
	break;
	case TypeMesh:
	{
		MeshData* md = (MeshData*)ptr;
		if (rc) md->RandomizeColor();
		else md->ToggleDisp();
	}
	break;
	case TypeAnnotations:
		((Annotations*)ptr)->ToggleDisp();
		break;
	case TypeGroup:
	{
		DataGroup* group = (DataGroup*)ptr;
		if (rc) group->RandomizeColor();
		else group->ToggleDisp();
	}
	break;
	case TypeMGroup:
	{
		MeshGroup* group = (MeshGroup*)ptr;
		if (rc) group->RandomizeColor();
		else group->ToggleDisp();
	}
	break;
	case TypeSegment:
	case TypeSegGroup:
	{
		VolumeData* vd = VolumeOf(ParentVolItem(sel_item));
		if (vd)
		{
			int id = vd->GetROIid(name.toStdWString());
			if (id != -1)
				vd->SetROISel(name.toStdWString(), !vd->isSelID(id));
		}
	}
	break;
	case TypeMeshSeg:
	case TypeMeshSegGroup:
	{
		MeshData* md = MeshOf(ParentMeshItem(sel_item));
		if (md)
		{
			const int id = sel_item->data(0, ROLE_ID).toInt();
			md->ToggleROIState(id);
			bool sync_group = false, sync_name = false;
			if (m_mesh_sync) m_mesh_sync(sync_group, sync_name);
			const bool state = md->GetROIState(id);
			if (sync_group) md->SetROIStateSiblings(id, state);
			if (sync_name) md->SetRoiStateByName(id, state);
		}
	}
	break;
	default:
		return;
	}
	if (rc)
		UpdateTree(name, type);
	else
		UpdateTreeIcons();
	UpdateSelection();
	RefreshViews();
}

// ---------------------------------------------------------------------------
// wx DeleteSelection
// ---------------------------------------------------------------------------

void TreePanel::DeleteSelection()
{
	if (m_fixed || !CurCanvas()) return;
	QTreeWidgetItem* sel_item = m_tree->currentItem();
	if (!sel_item) return;
	QTreeWidgetItem* par_item = sel_item->parent();
	if (!par_item) return;
	const int type = ItemType(sel_item);
	const int par_type = ItemType(par_item);
	const QString name = sel_item->text(0);
	void* ptr = reinterpret_cast<void*>(sel_item->data(0, ROLE_PTR).value<quintptr>());

	// wx: the next sibling is selected afterwards (else the selection is
	// rebuilt from scratch)
	QTreeWidgetItem* next_item = nullptr;
	{
		int idx = par_item->indexOfChild(sel_item);
		if (idx + 1 < par_item->childCount()) next_item = par_item->child(idx + 1);
		else if (idx > 0) next_item = par_item->child(idx - 1);   // last in its group: stay nearby
	}
	const QString next_name = next_item ? next_item->text(0) : QString();
	const int next_type = next_item ? ItemType(next_item) : -1;

	if (type == TypeSegment || type == TypeSegGroup)
	{
		VolumeData* vd = VolumeOf(ParentVolItem(sel_item));
		if (!vd) return;
		const int id = sel_item->data(0, ROLE_ID).toInt();
		int next_id = vd->GetNextSiblingROI(id);
		vd->SetROISel(name.toStdWString(), false, true);
		vd->EraseROITreeNode(name.toStdWString());
		UpdateTree();
		SelectROI(vd, next_id);
		RefreshViews();
		return;
	}

	if (par_type == TypeView)
	{
		if (type == TypeVolume)
		{
			VolumeData* vd = (VolumeData*)ptr;
			vd->SetDisp(true);
			Q_EMIT volumeRemoveRequested(vd);
		}
		else if (type == TypeMesh)
		{
			MeshData* md = (MeshData*)ptr;
			md->SetDisp(true);
			Q_EMIT meshRemoveRequested(md);
		}
		else if (type == TypeAnnotations)
		{
			Annotations* ann = (Annotations*)ptr;
			ann->SetDisp(true);
			Q_EMIT annotationsRemoveRequested(ann);
		}
		else if (type == TypeGroup || type == TypeMGroup)
		{
			std::vector<VolumeData*> vols;
			std::vector<MeshData*> meshes;
			CurCanvas()->RemoveGroup(V(name), &vols, &meshes);
			Q_EMIT groupRemoved(vols, meshes);
		}
	}
	else if (par_type == TypeGroup && type == TypeVolume)
		Q_EMIT volumeRemoveRequested((VolumeData*)ptr);
	else if (par_type == TypeMGroup && type == TypeMesh)
		Q_EMIT meshRemoveRequested((MeshData*)ptr);
	else
		return;

	if (next_item)
		UpdateTree(next_name, next_type);
	else
		UpdateTree(tr("Render View:1"), TypeView);   // wx: OnSelection(1)
	RefreshViews();
}

// ---------------------------------------------------------------------------
// wx OnEndDrag
// ---------------------------------------------------------------------------

void TreePanel::OnEndDrag(QTreeWidgetItem* src_item, QTreeWidgetItem* dst_item, int mouse_pos_y)
{
	// wx: every move stays inside one view (GetView(<view item text>))
	RenderCanvas* m_canvas = CanvasOf(src_item);
	if (m_fixed || !m_canvas || !src_item) return;
	// Qt extension: a volume / mesh dropped onto another view (its item, a
	// group or an item in it) moves to that view (wx does nothing then)
	if (dst_item && ItemType(src_item) != TypeView && CanvasOf(dst_item) != m_canvas &&
		(ItemType(src_item) == TypeVolume || ItemType(src_item) == TypeMesh))
	{
		MoveToView(src_item, dst_item);
		return;
	}
	QTreeWidgetItem* src_par_item = src_item->parent();
	QTreeWidgetItem* dst_par_item = dst_item ? dst_item->parent() : nullptr;
	auto text = [](QTreeWidgetItem* it) { return it ? it->text(0) : QString(); };
	auto vs = [](const QString& s) { return VVDString(s.toStdWString()); };

	QString update_name;
	int update_type = -1;
	VolumeData* roi_vd = nullptr;
	int roi_id = 0;
	bool changed = false;

	if (src_par_item && dst_item && dst_par_item)
	{
		// insert_mode: 0-before dst; 1-after dst
		const QRect dst_rect = m_tree->visualItemRect(dst_item);
		const double dst_center_y = (dst_rect.top() + dst_rect.bottom()) / 2.0;
		int insert_mode = 0;
		if (dst_rect.isValid() && mouse_pos_y > dst_center_y)
			insert_mode = 1;

		const int src_type = ItemType(src_item);
		const int src_par_type = ItemType(src_par_item);
		const int dst_type = ItemType(dst_item);
		const int dst_par_type = ItemType(dst_par_item);
		const QString src_name = text(src_item);
		const QString src_par_name = text(src_par_item);
		const QString dst_name = text(dst_item);
		const QString dst_par_name = text(dst_par_item);
		const QString empty;
		changed = true;

		if (src_par_type == TypeView && dst_par_type == TypeView &&
			src_par_name == dst_par_name && src_name != dst_name)
		{
			//move within the same view
			if (src_type == TypeVolume && dst_type == TypeGroup)
				m_canvas->MoveLayertoGroup(vs(dst_name), vs(src_name), vs(empty));   //move volume to the group in the same view
			else if (src_type == TypeMesh && dst_type == TypeMGroup)
				m_canvas->MoveMeshtoGroup(vs(dst_name), vs(src_name), vs(empty));    //move mesh into a group
			else
				m_canvas->MoveLayerinView(vs(src_name), vs(dst_name));
		}
		else if (src_par_type == TypeGroup && dst_par_type == TypeGroup &&
			src_par_name == dst_par_name && src_name != dst_name)
		{
			//move volume within the same group
			m_canvas->MoveLayerinGroup(vs(src_par_name), vs(src_name), vs(dst_name), insert_mode);
		}
		else if (src_par_type == TypeGroup && src_type == TypeVolume &&
			dst_par_type == TypeView && dst_par_name == text(src_par_item->parent()))
		{
			//move volume outside of the group
			if (dst_type == TypeGroup)
				m_canvas->MoveLayerfromtoGroup(vs(src_par_name), vs(dst_name), vs(src_name), vs(empty));
			else
				m_canvas->MoveLayertoView(vs(src_par_name), vs(src_name), vs(dst_name));
		}
		else if (src_par_type == TypeView && src_type == TypeVolume &&
			dst_par_type == TypeGroup && src_par_name == text(dst_par_item->parent()))
		{
			//move volume into group
			m_canvas->MoveLayertoGroup(vs(dst_par_name), vs(src_name), vs(dst_name));
		}
		else if (src_par_type == TypeGroup && src_type == TypeVolume &&
			dst_par_type == TypeGroup && dst_type == TypeVolume &&
			text(src_par_item->parent()) == text(dst_par_item->parent()) &&
			text(src_par_item) != text(dst_par_item))
		{
			//move volume from one group to another
			m_canvas->MoveLayerfromtoGroup(vs(src_par_name), vs(dst_par_name), vs(src_name), vs(dst_name), insert_mode);
		}
		else if (src_type == TypeVolume && src_par_type == TypeGroup &&
			dst_type == TypeView && text(src_par_item->parent()) == dst_name)
		{
			//move volume outside of the group
			m_canvas->MoveLayertoView(vs(src_par_name), vs(src_name), vs(empty));
		}
		else if (src_par_type == TypeMGroup && dst_par_type == TypeMGroup &&
			src_par_name == dst_par_name && src_name != dst_name)
		{
			//move mesh within the same group
			m_canvas->MoveMeshinGroup(vs(src_par_name), vs(src_name), vs(dst_name), insert_mode);
		}
		else if (src_par_type == TypeMGroup && src_type == TypeMesh &&
			dst_par_type == TypeView && dst_par_name == text(src_par_item->parent()))
		{
			//move mesh outside of the group
			if (dst_type == TypeMGroup)
				m_canvas->MoveMeshfromtoGroup(vs(src_par_name), vs(dst_name), vs(src_name), vs(empty));
			else
				m_canvas->MoveMeshtoView(vs(src_par_name), vs(src_name), vs(dst_name));
		}
		else if (src_par_type == TypeView && src_type == TypeMesh &&
			dst_par_type == TypeMGroup && src_par_name == text(dst_par_item->parent()))
		{
			//move mesh into group
			m_canvas->MoveMeshtoGroup(vs(dst_par_name), vs(src_name), vs(dst_name));
		}
		else if (src_par_type == TypeMGroup && src_type == TypeMesh &&
			dst_par_type == TypeMGroup && dst_type == TypeMesh &&
			text(src_par_item->parent()) == text(dst_par_item->parent()) &&
			text(src_par_item) != text(dst_par_item))
		{
			//move mesh from one group to another
			m_canvas->MoveMeshfromtoGroup(vs(src_par_name), vs(dst_par_name), vs(src_name), vs(dst_name), insert_mode);
		}
		else if (src_type == TypeMesh && src_par_type == TypeMGroup &&
			dst_type == TypeView && text(src_par_item->parent()) == dst_name)
		{
			//move mesh outside of the group
			m_canvas->MoveMeshtoView(vs(src_par_name), vs(src_name), vs(empty));
		}
		else if ((src_type == TypeSegment || src_type == TypeSegGroup) &&
			(dst_type == TypeSegment || dst_type == TypeSegGroup))
		{
			QTreeWidgetItem* s_vitem = ParentVolItem(src_item);
			QTreeWidgetItem* d_vitem = ParentVolItem(dst_item);
			if (s_vitem && s_vitem == d_vitem)
			{
				// wx OnDragging insert mode: segment: before / after; segment
				// group: upper quarter before, lower quarter after, else into
				int roi_insert = 0;
				const int h = dst_rect.height();
				if (dst_type == TypeSegment)
					roi_insert = mouse_pos_y > dst_center_y ? 1 : 0;
				else if (mouse_pos_y > dst_center_y + h / 4)
					roi_insert = 1;
				else if (mouse_pos_y < dst_center_y - h / 4)
					roi_insert = 0;
				else
					roi_insert = 2;
				roi_vd = VolumeOf(s_vitem);
				if (roi_vd)
					roi_vd->MoveROINode(src_item->data(0, ROLE_ID).toInt(), dst_item->data(0, ROLE_ID).toInt(), roi_insert);
			}
		}
		else if ((src_type == TypeSegment || src_type == TypeSegGroup) && dst_type == TypeVolume)
		{
			QTreeWidgetItem* s_vitem = ParentVolItem(src_item);
			if (s_vitem && s_vitem == dst_item)
			{
				roi_vd = VolumeOf(s_vitem);
				if (roi_vd)
					roi_vd->MoveROINode(src_item->data(0, ROLE_ID).toInt(), -1);
			}
		}

		if (src_type == TypeSegment || src_type == TypeSegGroup)
			roi_id = src_item->data(0, ROLE_ID).toInt();
		update_name = src_name;
		update_type = src_type;
	}
	else if (src_par_item && !dst_item)
	{
		//move volume out of the group (dropped on empty space)
		const int src_type = ItemType(src_item);
		const int src_par_type = ItemType(src_par_item);
		if (src_type == TypeVolume && src_par_type == TypeGroup)
		{
			m_canvas->MoveLayertoView(V(text(src_par_item)), V(text(src_item)), VVDString());
			update_name = text(src_item);
			update_type = src_type;
			changed = true;
		}
	}

	if (!changed)
	{
		UpdateSelection();
		return;
	}
	// after the drag (Qt still holds the items until the drop returns)
	QTimer::singleShot(0, this, [this, roi_vd, roi_id, update_name, update_type]() {
		if (roi_vd)
		{
			UpdateTree();
			SelectROI(roi_vd, roi_id);
		}
		else
			UpdateTree(update_name, update_type);
		RefreshViews();
	});
}

// ---------------------------------------------------------------------------
// rename (wx OnRenameMenu / OnRename / OnRenamed)
// ---------------------------------------------------------------------------

void TreePanel::OnRenameMenu()
{
	if (m_fixed) return;
	QTreeWidgetItem* sel_item = m_tree->currentItem();
	const int type = ItemType(sel_item);
	if (!sel_item || type == TypeRoot || type == TypeView)   // wx OnRename vetoes these
		return;
	m_rename_item = sel_item;
	m_rename_prev = sel_item->text(0);
	{
		QSignalBlocker b(m_tree);
		sel_item->setFlags(sel_item->flags() | Qt::ItemIsEditable);
	}
	m_tree->editItem(sel_item, 0);
}

void TreePanel::OnItemChanged(QTreeWidgetItem* item, int column)
{
	if (m_updating || column != 0 || !item || item != m_rename_item)
		return;
	m_rename_item = nullptr;
	const QString prev = m_rename_prev;
	const QString name = item->text(0);
	{
		QSignalBlocker b(m_tree);
		item->setFlags(item->flags() & ~Qt::ItemIsEditable);
		if (name.isEmpty()) item->setText(0, prev);
	}
	if (name.isEmpty() || name == prev)
		return;
	// the item is rebuilt by OnRenamed: not inside the editor's commit
	QTimer::singleShot(0, this, [this, item, prev, name]() { OnRenamed(item, prev, name); });
}

void TreePanel::OnRenamed(QTreeWidgetItem* item, const QString& prev_name, const QString& name_in)
{
	RenderCanvas* m_canvas = nullptr;
	for (RenderCanvas* c : m_canvases)
		if (c && (c->GetGroup(V(prev_name)) || c->GetMGroup(V(prev_name)))) m_canvas = c;
	if (!m_canvas) m_canvas = CurCanvas();
	if (!m_canvas || !m_mgr) return;
	// the item may be gone (tree rebuilt meanwhile): find it again
	QTreeWidgetItem* sel_item = nullptr;
	{
		QTreeWidgetItemIterator it(m_tree);
		while (*it) { if (*it == item) { sel_item = item; break; } ++it; }
	}
	if (!sel_item) return;
	const int type = ItemType(sel_item);
	void* ptr = reinterpret_cast<void*>(sel_item->data(0, ROLE_PTR).value<quintptr>());
	VVDString name = V(name_in);
	switch (type)
	{
	case TypeVolume:
	{
		VolumeData* vd = (VolumeData*)ptr;
		name = m_mgr->CheckNewName(name, DATA_VOLUME);
		vd->SetName(name);
		UpdateTree(Q(name), type);
	}
	break;
	case TypeMesh:
	{
		MeshData* md = (MeshData*)ptr;
		name = m_mgr->CheckNewName(name, DATA_MESH);
		md->SetName(name);
		UpdateTree(Q(name), type);
	}
	break;
	case TypeAnnotations:
	{
		Annotations* an = (Annotations*)ptr;
		name = m_mgr->CheckNewName(name, DATA_ANNOTATIONS);
		an->SetName(name);
		UpdateTree(Q(name), type);
	}
	break;
	case TypeGroup:
	{
		DataGroup* group = m_canvas->GetGroup(V(prev_name));
		if (!group) break;
		name = m_canvas->CheckNewGroupName(name, 5);
		group->SetName(name);
		UpdateTree(Q(name), type);
	}
	break;
	case TypeMGroup:
	{
		MeshGroup* mgroup = m_canvas->GetMGroup(V(prev_name));
		if (!mgroup) break;
		name = m_canvas->CheckNewGroupName(name, 6);
		mgroup->SetName(name);
		UpdateTree(Q(name), type);
	}
	break;
	case TypeSegment:
	case TypeSegGroup:
	{
		QTreeWidgetItem* vol_item = ParentVolItem(sel_item);
		VolumeData* vd = VolumeOf(vol_item);
		const int id = sel_item->data(0, ROLE_ID).toInt();
		if (vd && id != -1)
		{
			vd->SetROIName(name_in.toStdWString(), id);
			UpdateTree(name_in, type);
		}
	}
	break;
	}
	RefreshViews();
}

// ---------------------------------------------------------------------------
// wx OnContextMenu
// ---------------------------------------------------------------------------

QMenu* TreePanel::BuildContextMenu(QTreeWidgetItem* item)
{
	if (!item || m_fixed) return nullptr;
	auto* menu = new QMenu(this);
	auto add = [this, menu](const QString& text, void (TreePanel::*fn)()) {
		QAction* a = menu->addAction(text);
		connect(a, &QAction::triggered, this, fn);
		return a;
	};
	auto add_f = [this, menu](const QString& text, std::function<void()> fn) {
		QAction* a = menu->addAction(text);
		connect(a, &QAction::triggered, this, fn);
		return a;
	};
	auto tool = [this](int which) { return [this, which]() { Q_EMIT toolRequested(which); }; };
	const QString expand = item->isExpanded() ? tr("Collapse") : tr("Expand");
	switch (ItemType(item))
	{
	case TypeRoot:
		add(expand, &TreePanel::OnExpand);
		break;
	case TypeView:
		add(tr("Toggle Visibility"), &TreePanel::OnToggleDisp);
		add(expand, &TreePanel::OnExpand);
		menu->addSeparator();
		add(tr("Randomize Colors"), &TreePanel::OnRandomizeColor);
		add(tr("Add Volume Group"), &TreePanel::OnAddDataGroup);
		add(tr("Add Mesh Group"), &TreePanel::OnAddMeshGroup);
		// wx: "Close" for views other than the first (GetView(0)->GetName())
		if (!m_canvases.empty() && CanvasOf(item) != m_canvases[0])
			add(tr("Close"), &TreePanel::OnCloseView);
		break;
	case TypeVolume:
	{
		VolumeData* vd = VolumeOf(item);
		if (!vd) break;
		add(tr("Toggle Visibility"), &TreePanel::OnToggleDisp);
		add(tr("Rename"), &TreePanel::OnRenameMenu);
		add(tr("Duplicate"), &TreePanel::OnDuplicate);
		add(tr("Save"), &TreePanel::OnSave);
		add(tr("Save 3D Painted Region"), &TreePanel::OnSaveSegmentedVolume);
		// Qt only: the Fiji macro Color_Depth_MIP_batch (MainWindowCDM.cpp)
		add(tr("Save Color Depth MIP..."), &TreePanel::OnSaveColorDepthMIP);
		add_f(tr("Show Entire Volume"), [this]() { OnSetMaskHideMode(VOL_MASK_HIDE_NONE); });
		add_f(tr("Show Only Inside of Mask"), [this]() { OnSetMaskHideMode(VOL_MASK_HIDE_OUTSIDE); });
		add_f(tr("Show Only Outside of Mask"), [this]() { OnSetMaskHideMode(VOL_MASK_HIDE_INSIDE); });
		add(tr("Export Mask"), &TreePanel::OnExportMask);
		add(tr("Import Mask"), &TreePanel::OnImportMask);
		add(tr("Bake"), &TreePanel::OnBakeVolume);
		add(tr("Isolate"), &TreePanel::OnIsolate);
		add(tr("Show All"), &TreePanel::OnShowAll);
		add_f(tr("Flip Horizontally"), [this]() { OnFlip(true); });
		add_f(tr("Flip Vertically"), [this]() { OnFlip(false); });
		if (vd->GetColormapMode() == 3)
		{
			menu->addSeparator();
			add(tr("Add Segments"), &TreePanel::OnAddSegments);
			add(tr("Export Metadata"), &TreePanel::OnExportMetadata);
			add(tr("Add Segment Group"), &TreePanel::OnAddSegGroup);
			add_f(tr("Select Children"), [this]() { OnShowAllSegChildren(true); });
			add_f(tr("Deselect Children"), [this]() { OnShowAllSegChildren(false); });
			add(tr("Select All Named Segments"), &TreePanel::OnShowAllNamedSeg);
			add(tr("Deselect All Segments"), &TreePanel::OnHideAllSeg);
			add(tr("Delete All Segments"), &TreePanel::OnDeleteAllSeg);
		}
		menu->addSeparator();
		add(tr("Randomize Colors"), &TreePanel::OnRandomizeColor);
		add(tr("Set the Same Color to All Siblings"), &TreePanel::OnSetSameColor);
		add(tr("Add Volume Group"), &TreePanel::OnAddDataGroup);
		add(tr("Delete"), &TreePanel::DeleteSelection);
		menu->addSeparator();
		add_f(tr("Analyze..."), tool(0));
		add_f(tr("Information..."), tool(1));
		add_f(tr("Components && Tracking..."), tool(6));   // commented out in wx (TreePanel.cpp:515)
		add_f(tr("Noise Reduction..."), tool(2));
		add_f(tr("Counting and Volume..."), tool(3));
		add_f(tr("Colocalization Analysis..."), tool(4));
		add_f(tr("Convert..."), tool(5));
	}
	break;
	case TypeMesh:
		add(tr("Toggle Visibility"), &TreePanel::OnToggleDisp);
		add(tr("Rename"), &TreePanel::OnRenameMenu);
		add(tr("Save"), &TreePanel::OnSave);
		add(tr("Duplicate"), &TreePanel::OnDuplicate);
		add(tr("Isolate"), &TreePanel::OnIsolate);
		add(tr("Show All"), &TreePanel::OnShowAll);
		menu->addSeparator();
		add(tr("Randomize Colors"), &TreePanel::OnRandomizeColor);
		add(tr("Set the Same Color to All Siblings"), &TreePanel::OnSetSameColor);
		add(tr("Add Mesh Group"), &TreePanel::OnAddMeshGroup);
		add(tr("Delete"), &TreePanel::DeleteSelection);
		menu->addSeparator();
		add_f(tr("Manipulate"), [this]() { if (MeshData* md = CurrentMesh()) Q_EMIT manipulateRequested(md); });
		break;
	case TypeAnnotations:
		add(tr("Rename"), &TreePanel::OnRenameMenu);
		add(tr("Save"), &TreePanel::OnSave);
		add(tr("Export Segments"), &TreePanel::OnExportAllSegments);
		break;
	case TypeGroup:
		add(tr("Toggle Visibility"), &TreePanel::OnToggleDisp);
		add(tr("Rename"), &TreePanel::OnRenameMenu);
		add(expand, &TreePanel::OnExpand);
		menu->addSeparator();
		add(tr("Randomize Colors"), &TreePanel::OnRandomizeColor);
		add(tr("Set the Same Color to All Children"), &TreePanel::OnSetSameColor);
		add(tr("Add Volume Group"), &TreePanel::OnAddDataGroup);
		add(tr("Delete"), &TreePanel::DeleteSelection);
		break;
	case TypeMGroup:
		add(tr("Toggle Visibility"), &TreePanel::OnToggleDisp);
		add(tr("Rename"), &TreePanel::OnRenameMenu);
		add(expand, &TreePanel::OnExpand);
		menu->addSeparator();
		add(tr("Randomize Colors"), &TreePanel::OnRandomizeColor);
		add(tr("Set the Same Color to All Children"), &TreePanel::OnSetSameColor);
		add(tr("Add Mesh Group"), &TreePanel::OnAddMeshGroup);
		add(tr("Delete"), &TreePanel::DeleteSelection);
		break;
	case TypeSegment:
		add(tr("Toggle Visibility"), &TreePanel::OnToggleDisp);
		add(tr("Add Segment Group"), &TreePanel::OnAddSegGroup);
		add(tr("Delete"), &TreePanel::DeleteSelection);
		break;
	case TypeSegGroup:
		add(tr("Toggle Visibility"), &TreePanel::OnToggleDisp);
		add(tr("Add Segment Group"), &TreePanel::OnAddSegGroup);
		add_f(tr("Select Children"), [this]() { OnShowAllSegChildren(true); });
		add_f(tr("Deselect Children"), [this]() { OnShowAllSegChildren(false); });
		add(tr("Delete"), &TreePanel::DeleteSelection);
		break;
	}
	return menu;
}

void TreePanel::OnContextMenu(const QPoint& pos)
{
	if (m_fixed) return;
	QTreeWidgetItem* item = m_tree->itemAt(pos);
	if (!item) return;
	m_tree->setCurrentItem(item);   // wx: SelectItem(sel_item)
	QMenu* menu = BuildContextMenu(item);
	if (!menu) return;
	if (menu->actions().isEmpty()) { menu->deleteLater(); return; }   // SWC segments: no menu (wx)
	menu->exec(m_tree->viewport()->mapToGlobal(pos));
	menu->deleteLater();
}

// ---------------------------------------------------------------------------
// context menu actions
// ---------------------------------------------------------------------------

void TreePanel::OnExpand()
{
	QTreeWidgetItem* sel_item = m_tree->currentItem();
	if (sel_item) sel_item->setExpanded(!sel_item->isExpanded());
}

// wx OnDuplicate
void TreePanel::OnDuplicate()
{
	if (m_fixed || !m_mgr || !CurCanvas()) return;
	QTreeWidgetItem* sel_item = m_tree->currentItem();
	if (VolumeData* vd = VolumeOf(sel_item))
	{
		VolumeData* vd_add = m_mgr->DuplicateVolumeData(vd, false);
		if (!vd_add) return;
		DataGroup* group = CurCanvas()->AddVolumeData(vd_add);
		Q_EMIT selectionChanged(2, group, vd_add, nullptr);
		UpdateTree(Q(vd->GetName()), TypeVolume);
	}
	else if (MeshData* md = MeshOf(sel_item))
	{
		MeshData* md_add = m_mgr->DuplicateMeshData(md, false);
		if (!md_add) return;
		CurCanvas()->AddMeshData(md_add);
		Q_EMIT selectionChanged(3, nullptr, nullptr, md_add);
		UpdateTree(Q(md_add->GetName()), TypeMesh);
	}
	RefreshViews();
}

// wx OnSave: volume (TIFF / TIFF sequence / NRRD), mesh (OBJ), annotations (TXT)
void TreePanel::OnSave()
{
	if (m_fixed) return;
	QTreeWidgetItem* sel_item = m_tree->currentItem();
	if (!sel_item) return;
	const int type = ItemType(sel_item);
	void* ptr = reinterpret_cast<void*>(sel_item->data(0, ROLE_PTR).value<quintptr>());
	if (type == TypeVolume)
	{
		VolumeData* vd = (VolumeData*)ptr;
		QStringList filters;
		filters << tr("Muti-page Tiff file (*.tif *.tiff)") << tr("Single-page Tiff sequence (*.tif)");
		if (!vd->isBrxml()) filters << tr("Nrrd file (*.nrrd)");
		// wx: + CreateExtraControl (crop to the clipping planes, pyramid level)
		int mode = 0;
		QString filename = FileDialogs::SaveVolume(this, "SAVEVOLUME", tr("Save Volume Data"), filters, &mode);
		if (filename.isEmpty()) return;
		vd->Save(V(filename), mode, false, true, true, true, CurCanvas() ? CurCanvas()->GetLoader() : nullptr,
			FileDialogs::GetCropToClip(), FileDialogs::GetScaleLevel());
		if (vd->isBrxml()) RefreshViews();
	}
	else if (type == TypeMesh)
	{
		QString filename = QFileDialog::getSaveFileName(this, tr("Save Mesh Data"), QString(), tr("OBJ file (*.obj)"));
		if (filename.isEmpty()) return;
		((MeshData*)ptr)->Save(V(filename));
	}
	else if (type == TypeAnnotations)
	{
		QString filename = QFileDialog::getSaveFileName(this, tr("Save Annotations"), QString(), tr("Text file (*.txt)"));
		if (filename.isEmpty()) return;
		((Annotations*)ptr)->Save(V(filename));
	}
}

bool TreePanel::RunFileAction(const QString& action)
{
	if (action == "savevolume") OnSave();
	else if (action == "savesegmented") OnSaveSegmentedVolume();
	else if (action == "bake") OnBakeVolume();
	else if (action == "exportmeta") OnExportMetadata();
	else return false;
	return true;
}

// wx OnSaveSegmentedVolume: save with the outside of the mask hidden
void TreePanel::OnSaveSegmentedVolume()
{
	if (m_fixed) return;
	VolumeData* vd = VolumeOf(m_tree->currentItem());
	if (!vd) return;
	QStringList filters;
	if (vd->isBrxml())
		filters << tr("Single-page Tiff sequence (*.tif)");
	else
		filters << tr("Muti-page Tiff file (*.tif *.tiff)") << tr("Single-page Tiff sequence (*.tif)") << tr("Nrrd file (*.nrrd)");
	int mode = 0;
	QString filename = FileDialogs::SaveVolume(this, "SAVESEGMENTED", tr("Save Volume Data"), filters, &mode);
	if (filename.isEmpty()) return;
	if (vd->isBrxml()) mode = 1;
	auto mskmode = vd->GetMaskHideMode();
	vd->SetMaskHideMode(VOL_MASK_HIDE_OUTSIDE);
	vd->Save(V(filename), mode, false, Compression(), true, true, CurCanvas() ? CurCanvas()->GetLoader() : nullptr,
		FileDialogs::GetCropToClip(), FileDialogs::GetScaleLevel());
	vd->SetMaskHideMode(mskmode);
	if (vd->isBrxml()) RefreshViews();
}

// Qt only: "Save Color Depth MIP..." (MainWindow::SaveColorDepthMIP)
void TreePanel::OnSaveColorDepthMIP()
{
	if (m_fixed) return;
	if (VolumeData* vd = VolumeOf(m_tree->currentItem()))
		Q_EMIT colorDepthMipRequested(vd);
}

// wx OnShowEntireVolume / OnHideOutsideOfMask / OnHideInsideOfMask
void TreePanel::OnSetMaskHideMode(int mode)
{
	if (m_fixed) return;
	VolumeData* vd = VolumeOf(m_tree->currentItem());
	if (!vd) return;
	vd->SetMaskHideMode(mode);
	RefreshViews();
}

void TreePanel::OnExportMask()
{
	if (m_fixed) return;
	VolumeData* vd = VolumeOf(m_tree->currentItem());
	if (!vd) return;
	QString filename = QFileDialog::getSaveFileName(this, tr("Export Mask Data"), QString(), tr("Nrrd file (*.nrrd)"));
	if (!filename.isEmpty())
		vd->ExportMask(V(filename));
}

void TreePanel::OnImportMask()
{
	if (m_fixed) return;
	VolumeData* vd = VolumeOf(m_tree->currentItem());
	if (!vd) return;
	QString filename = QFileDialog::getOpenFileName(this, tr("Import Mask Data"), QString(), tr("All Supported (*.msk *.nrrd)"));
	if (!filename.isEmpty())
		vd->ImportMask(V(filename));
	RefreshViews();
}

// wx OnBakeVolume: save with the volume properties applied
void TreePanel::OnBakeVolume()
{
	if (m_fixed) return;
	VolumeData* vd = VolumeOf(m_tree->currentItem());
	if (!vd || vd->isBrxml()) return;
	QStringList filters;
	filters << tr("Muti-page Tiff file (*.tif *.tiff)") << tr("Single-page Tiff sequence (*.tif)") << tr("Nrrd file (*.nrrd)");
	// wx shows the same options box here, but always crops (crop = true)
	// and saves level 0, whatever the box says
	int mode = 0;
	QString filename = FileDialogs::SaveVolume(this, "BAKEVOLUME", tr("Bake Volume Data"), filters, &mode);
	if (filename.isEmpty()) return;
	vd->Save(V(filename), mode, true, Compression(), false, false, nullptr, true);
}

// wx OnIsolate / OnShowAll (the item's view)
void TreePanel::OnIsolate()
{
	if (m_fixed || !CurCanvas()) return;
	QTreeWidgetItem* sel_item = m_tree->currentItem();
	if (!sel_item) return;
	CurCanvas()->Isolate(ItemType(sel_item), V(sel_item->text(0)));
	UpdateTreeIcons();
	UpdateSelection();
	RefreshViews();
}

void TreePanel::OnShowAll()
{
	if (m_fixed || !CurCanvas()) return;
	CurCanvas()->ShowAll();
	UpdateTreeIcons();
	UpdateSelection();
	RefreshViews();
}

void TreePanel::OnFlip(bool horizontal)
{
	VolumeData* vd = VolumeOf(m_tree->currentItem());
	if (!vd) return;
	if (horizontal) vd->FlipHorizontally();
	else vd->FlipVertically();
	RefreshViews();
}

// wx OnAddSegments
void TreePanel::OnAddSegments()
{
	if (m_fixed) return;
	VolumeData* vd = VolumeOf(m_tree->currentItem());
	if (vd && vd->GetColormapMode() == 3)
		vd->GenAllROINames();
	RefreshViews();
	UpdateTree();
}

// wx OnExportMetadata
void TreePanel::OnExportMetadata()
{
	if (m_fixed) return;
	VolumeData* vd = VolumeOf(m_tree->currentItem());
	if (!vd) return;
	if (vd->isBrxml())
	{
		QString filename = FileDialogs::SaveVolume(this, "EXPORTMETA", tr("Save a VVD file with Metadata"),
			QStringList() << tr("VVD file (*.vvd)"), nullptr);
		if (filename.isEmpty()) return;
		BRKXMLReader* br = (BRKXMLReader*)vd->GetReader();
		tinyxml2::XMLDocument* doc = br->GetVVDXMLDoc();
		BRKXMLWriter bw;
		std::wstring rtree = vd->ExportROITree();
		if (!rtree.empty())
		{
			bw.AddROITreeToMetadata(rtree);
			// "Save metadata as an individual file" (wx m_md_save_indv)
			if (FileDialogs::GetMetadataSeparate())
				bw.SaveVVDXML_ExternalMetadata(filename.toStdWString(), doc);
			else
				bw.SaveVVDXML_Metadata(filename.toStdWString(), doc);
		}
	}
	else
	{
		QString filename = QFileDialog::getSaveFileName(this, tr("Save Metadata"), "_metadata.xml", tr("XML file (*.xml)"));
		if (filename.isEmpty()) return;
		BRKXMLWriter bw;
		std::wstring rtree = vd->ExportROITree();
		if (!rtree.empty())
		{
			bw.AddROITreeToMetadata(rtree);
			bw.SaveMetadataXML(filename.toStdWString());
		}
	}
}

// wx OnAddSegGroup
void TreePanel::OnAddSegGroup()
{
	if (m_fixed) return;
	QTreeWidgetItem* sel_item = m_tree->currentItem();
	const int type = ItemType(sel_item);
	if (type == TypeVolume)
	{
		VolumeData* vd = VolumeOf(sel_item);
		if (!vd) return;
		int gid = vd->AddROIGroup();
		vd->AddSelID(gid);
		UpdateTree();
		SelectROI(vd, gid);
	}
	else if (type == TypeSegment || type == TypeSegGroup)
	{
		VolumeData* vd = VolumeOf(ParentVolItem(sel_item));
		if (!vd) return;
		QTreeWidgetItem* pitem = sel_item->parent();
		if (!pitem) return;
		QString parent_name;
		if (type == TypeSegment)
		{
			if (ItemType(pitem) == TypeSegGroup)
				parent_name = pitem->text(0);
		}
		else
			parent_name = sel_item->text(0);
		int gid = vd->AddROIGroup(parent_name.toStdWString());
		vd->AddSelID(gid);
		UpdateTree();
		SelectROI(vd, gid);
	}
}

// wx OnShowAllSegChildren / OnHideAllSegChildren
void TreePanel::OnShowAllSegChildren(bool show)
{
	if (m_fixed) return;
	QTreeWidgetItem* sel_item = m_tree->currentItem();
	const int type = ItemType(sel_item);
	if (type == TypeSegGroup)
	{
		VolumeData* vd = VolumeOf(ParentVolItem(sel_item));
		if (vd)
		{
			const std::wstring n = sel_item->text(0).toStdWString();
			int id = vd->GetROIid(n);
			if (id != -1)
			{
				if (show) vd->SetROISel(n, true);
				vd->SetROISelChildren(n, show);
			}
		}
	}
	else if (type == TypeVolume)
	{
		if (VolumeData* vd = VolumeOf(sel_item))
			vd->SetROISelChildren(std::wstring(), show);
	}
	else
		return;
	UpdateTreeIcons();
	RefreshViews();
}

void TreePanel::OnShowAllNamedSeg()
{
	if (m_fixed) return;
	QTreeWidgetItem* sel_item = m_tree->currentItem();
	VolumeData* vd = ItemType(sel_item) == TypeSegGroup ? VolumeOf(ParentVolItem(sel_item)) : VolumeOf(sel_item);
	if (!vd) return;
	vd->SelectAllNamedROI();
	UpdateTreeIcons();
	RefreshViews();
}

void TreePanel::OnHideAllSeg()
{
	if (m_fixed) return;
	QTreeWidgetItem* sel_item = m_tree->currentItem();
	VolumeData* vd = ItemType(sel_item) == TypeSegGroup ? VolumeOf(ParentVolItem(sel_item)) : VolumeOf(sel_item);
	if (!vd) return;
	vd->DeselectAllROI();
	UpdateTreeIcons();
	RefreshViews();
}

void TreePanel::OnDeleteAllSeg()
{
	if (m_fixed) return;
	if (QMessageBox::question(this, tr("Delete all segments"), tr("Do you want to proceed?"),
		QMessageBox::Ok | QMessageBox::Cancel) != QMessageBox::Ok)
		return;
	QTreeWidgetItem* sel_item = m_tree->currentItem();
	VolumeData* vd = ItemType(sel_item) == TypeSegGroup ? VolumeOf(ParentVolItem(sel_item)) : VolumeOf(sel_item);
	if (!vd) return;
	vd->ClearROIs();
	UpdateTree(Q(vd->GetName()), TypeVolume);
	RefreshViews();
}

// wx OnRandomizeColor
void TreePanel::OnRandomizeColor()
{
	if (m_fixed || !CurCanvas()) return;
	QTreeWidgetItem* sel_item = m_tree->currentItem();
	if (!sel_item) return;
	const int type = ItemType(sel_item);
	void* ptr = reinterpret_cast<void*>(sel_item->data(0, ROLE_PTR).value<quintptr>());
	const QString name = sel_item->text(0);
	switch (type)
	{
	case TypeView: CurCanvas()->RandomizeColor(); break;
	case TypeVolume: ((VolumeData*)ptr)->RandomizeColor(); break;
	case TypeMesh: ((MeshData*)ptr)->RandomizeColor(); break;
	case TypeGroup: ((DataGroup*)ptr)->RandomizeColor(); break;
	case TypeMGroup: ((MeshGroup*)ptr)->RandomizeColor(); break;
	default: return;
	}
	UpdateTree(name, type);
	RefreshViews();
}

// wx SetSameColorToAllDatasetsInGroup
void TreePanel::OnSetSameColor()
{
	if (m_fixed) return;
	QTreeWidgetItem* sel_item = m_tree->currentItem();
	if (!sel_item) return;
	const int type = ItemType(sel_item);
	const QString name = sel_item->text(0);
	if (type == TypeVolume)
	{
		VolumeData* vd = VolumeOf(sel_item);
		FLIVR::Color c = vd->GetColor();
		QTreeWidgetItem* par_item = sel_item->parent();
		for (int i = 0; par_item && i < par_item->childCount(); i++)
			if (VolumeData* c_vd = VolumeOf(par_item->child(i)))
				c_vd->SetColor(c);
	}
	else if (type == TypeMesh)
	{
		MeshData* md = MeshOf(sel_item);
		FLIVR::Color amb, diff, spec;
		double shine, alpha;
		md->GetMaterial(amb, diff, spec, shine, alpha);
		QTreeWidgetItem* par_item = sel_item->parent();
		for (int i = 0; par_item && i < par_item->childCount(); i++)
			if (MeshData* c_md = MeshOf(par_item->child(i)))
				c_md->SetMaterial(amb, diff, spec);
	}
	else if (type == TypeGroup)
	{
		// wx: the first child's colour to all children
		FLIVR::Color c;
		bool first = true;
		for (int i = 0; i < sel_item->childCount(); i++)
		{
			VolumeData* c_vd = VolumeOf(sel_item->child(i));
			if (!c_vd) continue;
			if (first) { c = c_vd->GetColor(); first = false; }
			else c_vd->SetColor(c);
		}
	}
	else if (type == TypeMGroup)
	{
		FLIVR::Color amb, diff, spec;
		double shine, alpha;
		bool first = true;
		for (int i = 0; i < sel_item->childCount(); i++)
		{
			MeshData* c_md = MeshOf(sel_item->child(i));
			if (!c_md) continue;
			if (first) { c_md->GetMaterial(amb, diff, spec, shine, alpha); first = false; }
			else c_md->SetMaterial(amb, diff, spec);
		}
	}
	else
		return;
	UpdateTree(name, type);
	RefreshViews();
}

// wx OnAddDataGroup / OnAddMeshGroup: a new group in the item's view
void TreePanel::OnAddDataGroup()
{
	if (m_fixed || !CurCanvas()) return;
	QTreeWidgetItem* sel_item = m_tree->currentItem();
	const int type = ItemType(sel_item);
	if (type != TypeView && type != TypeVolume && type != TypeMesh && type != TypeGroup && type != TypeMGroup && sel_item)
		return;
	CurCanvas()->AddGroup();
	// wx appends the group item to the view item (the selection stays);
	// the layer list keeps the group at the end, so a rebuild shows the
	// same tree
	UpdateTree();
}

void TreePanel::OnAddMeshGroup()
{
	if (m_fixed || !CurCanvas()) return;
	QTreeWidgetItem* sel_item = m_tree->currentItem();
	const int type = ItemType(sel_item);
	if (type != TypeView && type != TypeVolume && type != TypeMesh && type != TypeGroup && type != TypeMGroup && sel_item)
		return;
	CurCanvas()->AddMGroup();
	UpdateTree();
}

// wx OnExportAllSegments
void TreePanel::OnExportAllSegments()
{
	QTreeWidgetItem* sel_item = m_tree->currentItem();
	if (ItemType(sel_item) != TypeAnnotations) return;
	Annotations* ann = reinterpret_cast<Annotations*>(sel_item->data(0, ROLE_PTR).value<quintptr>());
	VolumeData* vd = ann ? ann->GetVolume() : nullptr;
	if (!vd) return;
	QString out_dir = QFileDialog::getExistingDirectory(this, tr("Save segments in"));
	if (out_dir.isEmpty()) return;
	vd->ExportEachSegment(V(out_dir), ann->GetLabel());
}

// ---------------------------------------------------------------------------
// testing
// ---------------------------------------------------------------------------

bool TreePanel::SimulateDrag(QTreeWidgetItem* src, QTreeWidgetItem* dst, bool after)
{
	if (!src) return false;
	QPoint pos;
	if (dst)
	{
		m_tree->scrollToItem(dst);
		const QRect r = m_tree->visualItemRect(dst);
		pos = QPoint(r.center().x(), after ? r.bottom() - 2 : r.top() + 2);
	}
	else
	{
		// below the last visible item
		QTreeWidgetItem* last = m_tree->topLevelItem(m_tree->topLevelItemCount() - 1);
		while (last && last->isExpanded() && last->childCount() > 0)
			last = last->child(last->childCount() - 1);
		const QRect r = last ? m_tree->visualItemRect(last) : QRect();
		pos = QPoint(20, r.bottom() + 6);
		if (pos.y() >= m_tree->viewport()->height())
			return false;
	}
	m_tree->setCurrentItem(src);
	m_tree->SetDragItem(src);
	QMimeData mime;
	mime.setData("application/x-vvd-tree-item", src->text(0).toUtf8());
	QDragEnterEvent enter(pos, Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
	QCoreApplication::sendEvent(m_tree->viewport(), &enter);
	QDragMoveEvent move(pos, Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
	QCoreApplication::sendEvent(m_tree->viewport(), &move);
	QDropEvent drop(QPointF(pos), Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
	QCoreApplication::sendEvent(m_tree->viewport(), &drop);
	m_tree->SetDragItem(nullptr);
	return drop.isAccepted();
}

// ---------------------------------------------------------------------------
// wx DataTreeCtrl::SelectROI(MeshData*, id): the segment item of an SWC mesh;
// the lazily built levels above it are loaded and expanded on the way
// ---------------------------------------------------------------------------

void TreePanel::SelectROI(MeshData* md, int id)
{
	if (!md || id < 0) return;
	QTreeWidgetItem* mitem = nullptr;
	QTreeWidgetItemIterator it(m_tree);
	while (*it) { if (MeshOf(*it) == md) { mitem = *it; break; } ++it; }
	if (!mitem) return;
	QTreeWidgetItem* ritem = FindItemBySegmentID(mitem, id);
	if (!ritem)
	{
		QString path = QString::fromStdWString(md->GetROIPath(id));
		if (path.isEmpty())
		{
			m_tree->setCurrentItem(mitem);
			return;
		}
		// ancestors of the segment, nearest first, down to a loaded one
		QTreeWidgetItem* pitem = nullptr;
		std::vector<int> pids;
		path = path.left(path.lastIndexOf('.') < 0 ? 0 : path.lastIndexOf('.'));
		while (!path.isEmpty())
		{
			const int pid = md->GetROIid(path.toStdWString());
			pitem = FindItemBySegmentID(mitem, pid);
			if (pitem) break;
			pids.push_back(pid);
			path = path.left(path.lastIndexOf('.') < 0 ? 0 : path.lastIndexOf('.'));
		}
		if (!pitem) pitem = mitem;
		ExpandROITree(pitem);
		for (auto i = pids.rbegin(); i != pids.rend() && pitem; ++i)
		{
			pitem = FindItemBySegmentID(pitem, *i);
			if (pitem) ExpandROITree(pitem);
		}
		if (pitem) ritem = FindItemBySegmentID(pitem, id);
	}
	if (ritem)
	{
		const bool was = m_updating;
		m_updating = true;
		TraversalExpand(ritem);
		m_updating = was;
		m_tree->setCurrentItem(ritem);
		m_tree->scrollToItem(ritem);
	}
	else
		m_tree->setCurrentItem(mitem);
}

// ---------------------------------------------------------------------------
// wx DataTreeCtrl visibility history
// ---------------------------------------------------------------------------

// wx GetVisHistoryTraversal: every item but the segment items, by name
void TreePanel::GetVisHistoryTraversal(QTreeWidgetItem* item)
{
	if (!item) return;
	for (int i = 0; i < item->childCount(); i++)
		GetVisHistoryTraversal(item->child(i));
	const int type = ItemType(item);
	if (type < 0 || type == TypeSegment || type == TypeSegGroup || type == TypeMeshSeg || type == TypeMeshSegGroup)
		return;
	void* ptr = reinterpret_cast<void*>(item->data(0, ROLE_PTR).value<quintptr>());
	VisState state;
	switch (type)
	{
	case TypeRoot: state.disp = true; break;
	case TypeView: state.disp = ptr ? ((RenderCanvas*)ptr)->GetDraw() : true; break;
	case TypeVolume:
	{
		VolumeData* vd = (VolumeData*)ptr;
		state.disp = vd->GetDisp();
		state.na_mode = vd->GetNAMode();
		state.v_sel_ids = vd->GetSelIDs();
		if (std::set<int>* ids = vd->GetActiveSegIDs())
			for (int id : *ids)
				state.label[id] = vd->GetSegmentMask(id);
	}
	break;
	case TypeMesh:
	{
		MeshData* md = (MeshData*)ptr;
		state.disp = md->GetDisp();
		if (md->isTree() && md->GetMR())
			state.mesh_segs = md->GetMR()->get_all_roi_state();
	}
	break;
	case TypeAnnotations: state.disp = ((Annotations*)ptr)->GetDisp(); break;
	case TypeGroup: state.disp = ((DataGroup*)ptr)->GetDisp(); break;
	case TypeMGroup: state.disp = ((MeshGroup*)ptr)->GetDisp(); break;
	}
	m_vtmp[item->text(0)] = state;
}

// wx SetVisHistoryTraversal
void TreePanel::SetVisHistoryTraversal(QTreeWidgetItem* item)
{
	if (!item) return;
	for (int i = 0; i < item->childCount(); i++)
		SetVisHistoryTraversal(item->child(i));
	auto f = m_vtmp.find(item->text(0));
	if (f == m_vtmp.end()) return;
	const VisState& state = f->second;
	void* ptr = reinterpret_cast<void*>(item->data(0, ROLE_PTR).value<quintptr>());
	switch (ItemType(item))
	{
	case TypeView: if (ptr) ((RenderCanvas*)ptr)->SetDraw(state.disp); break;
	case TypeVolume:
	{
		VolumeData* vd = (VolumeData*)ptr;
		vd->SetDisp(state.disp);
		vd->SetSelIDs(state.v_sel_ids);
		for (const auto& l : state.label)
			vd->SetSegmentMask(l.first, l.second);
	}
	break;
	case TypeMesh:
	{
		MeshData* md = (MeshData*)ptr;
		md->SetDisp(state.disp);
		if (md->isTree() && md->GetMR())
			md->GetMR()->set_all_roi_state(state.mesh_segs);
	}
	break;
	case TypeAnnotations:
		((Annotations*)ptr)->SetDisp(state.disp);
		Q_EMIT annotationsChanged();
		break;
	case TypeGroup: ((DataGroup*)ptr)->SetDisp(state.disp); break;
	case TypeMGroup: ((MeshGroup*)ptr)->SetDisp(state.disp); break;
	}
}

void TreePanel::PushVisHistory()
{
	m_vtmp.clear();
	for (int i = 0; i < m_tree->topLevelItemCount(); i++)
		GetVisHistoryTraversal(m_tree->topLevelItem(i));
	m_v_undo.push_back(m_vtmp);
	while (m_v_undo.size() > 10)
		m_v_undo.erase(m_v_undo.begin());
	m_v_redo.clear();
}

void TreePanel::UndoVisibility()
{
	if (m_v_undo.empty()) return;
	m_vtmp.clear();
	for (int i = 0; i < m_tree->topLevelItemCount(); i++)
		GetVisHistoryTraversal(m_tree->topLevelItem(i));
	m_v_redo.push_back(m_vtmp);
	m_vtmp = m_v_undo.back();
	for (int i = 0; i < m_tree->topLevelItemCount(); i++)
		SetVisHistoryTraversal(m_tree->topLevelItem(i));
	m_v_undo.pop_back();
	UpdateTreeIcons();
	RefreshViews();
}

void TreePanel::RedoVisibility()
{
	if (m_v_redo.empty()) return;
	m_vtmp.clear();
	for (int i = 0; i < m_tree->topLevelItemCount(); i++)
		GetVisHistoryTraversal(m_tree->topLevelItem(i));
	m_v_undo.push_back(m_vtmp);
	m_vtmp = m_v_redo.back();
	for (int i = 0; i < m_tree->topLevelItemCount(); i++)
		SetVisHistoryTraversal(m_tree->topLevelItem(i));
	m_v_redo.pop_back();
	UpdateTreeIcons();
	RefreshViews();
}

void TreePanel::ClearVisHistory()
{
	m_v_undo.clear();
	m_v_redo.clear();
}

// ---------------------------------------------------------------------------
// wx DataTreeCtrl::ShowAllDatasets / HideOtherDatasets / HideOtherVolumes /
// HideSelectedItem
// ---------------------------------------------------------------------------

void TreePanel::ShowAllDatasetsTraversal(QTreeWidgetItem* item)
{
	if (!item) return;
	for (int i = 0; i < item->childCount(); i++)
		ShowAllDatasetsTraversal(item->child(i));
	void* ptr = reinterpret_cast<void*>(item->data(0, ROLE_PTR).value<quintptr>());
	switch (ItemType(item))
	{
	case TypeVolume: ((VolumeData*)ptr)->SetDisp(true); break;
	case TypeMesh: ((MeshData*)ptr)->SetDisp(true); break;
	case TypeAnnotations:
		((Annotations*)ptr)->SetDisp(true);
		Q_EMIT annotationsChanged();
		break;
	}
}

void TreePanel::ShowAllDatasets()
{
	QTreeWidgetItem* item = m_tree->currentItem();
	if (!item || !CurCanvas()) return;
	PushVisHistory();
	bool sub_groups = false;
	const int type = ItemType(item);
	switch (type)
	{
	case TypeVolume:
	{
		VolumeData* vd = VolumeOf(item);
		if (vd && vd->GetColormapMode() == 3)
		{
			vd->ClearSelIDs();
			vd->SetROISel(L"", true, true);
			if (vd->GetIDColDispMode() == 2)
			{
				vd->SetIDColDispMode(0);
				Q_EMIT roiDisplayChanged();
			}
			sub_groups = true;
		}
	}
	break;
	case TypeSegment:
	case TypeSegGroup:
	{
		VolumeData* vd = VolumeOf(ParentVolItem(item));
		if (vd && vd->GetColormapMode() == 3)
		{
			vd->ClearSelIDs();
			vd->SetROISel(L"", true, true);
			sub_groups = true;
		}
	}
	break;
	case TypeMesh:
	{
		MeshData* md = MeshOf(item);
		if (md && md->isTree())
		{
			md->SetROIStateTraverse(true);
			sub_groups = true;
		}
	}
	break;
	case TypeMeshSeg:
	case TypeMeshSegGroup:
	{
		MeshData* md = MeshOf(ParentMeshItem(item));
		if (md && md->isTree())
		{
			md->SetROIStateTraverse(true);
			sub_groups = true;
		}
	}
	break;
	}
	if (!sub_groups)
		for (int i = 0; i < m_tree->topLevelItemCount(); i++)
			ShowAllDatasetsTraversal(m_tree->topLevelItem(i));
	UpdateTreeIcons();
	RefreshViews();
}

// wx HideOtherDatasetsTraversal (volumes, meshes, annotations) /
// HideOtherVolumesTraversal (volumes only): everything but self is hidden
void TreePanel::HideOtherDatasetsTraversal(QTreeWidgetItem* item, QTreeWidgetItem* self, bool volumes_only)
{
	if (!item) return;
	for (int i = 0; i < item->childCount(); i++)
		HideOtherDatasetsTraversal(item->child(i), self, volumes_only);
	if (item == self) return;
	void* ptr = reinterpret_cast<void*>(item->data(0, ROLE_PTR).value<quintptr>());
	switch (ItemType(item))
	{
	case TypeVolume: ((VolumeData*)ptr)->SetDisp(false); break;
	case TypeMesh: if (!volumes_only) ((MeshData*)ptr)->SetDisp(false); break;
	case TypeAnnotations:
		if (!volumes_only)
		{
			((Annotations*)ptr)->SetDisp(false);
			Q_EMIT annotationsChanged();
		}
		break;
	}
}

void TreePanel::HideOtherDatasets(QTreeWidgetItem* item)
{
	if (!CurCanvas()) return;
	if (!item) item = m_tree->currentItem();
	if (!item) return;
	const int type = ItemType(item);
	if (type != TypeVolume && type != TypeMesh && type != TypeAnnotations && type != TypeSegment &&
		type != TypeSegGroup && type != TypeMeshSeg && type != TypeMeshSegGroup)
		return;
	PushVisHistory();
	bool sub_groups = false;
	switch (type)
	{
	case TypeVolume:
	{
		VolumeData* vd = VolumeOf(item);
		if (!vd) return;
		if (!vd->GetDisp())
			vd->SetDisp(true);
		else if (vd->GetColormapMode() == 3)
		{
			vd->SetIDColDispMode(2);
			sub_groups = true;
			Q_EMIT roiDisplayChanged();
		}
	}
	break;
	case TypeMesh:
	{
		MeshData* md = MeshOf(item);
		if (!md) return;
		if (!md->GetDisp())
			md->SetDisp(true);
	}
	break;
	case TypeAnnotations:
	{
		Annotations* ann = reinterpret_cast<Annotations*>(item->data(0, ROLE_PTR).value<quintptr>());
		if (!ann || !ann->GetDisp())
			return;
	}
	break;
	case TypeSegment:
	case TypeSegGroup:
	{
		VolumeData* vd = VolumeOf(ParentVolItem(item));
		if (vd)
		{
			const std::wstring ws_name = item->text(0).toStdWString();
			vd->ClearSelIDs();
			vd->SetROISel(ws_name, true, true);
		}
		sub_groups = true;
	}
	break;
	case TypeMeshSeg:
	case TypeMeshSegGroup:
	{
		MeshData* md = MeshOf(ParentMeshItem(item));
		if (md)
		{
			const int id = item->data(0, ROLE_ID).toInt();
			md->SetROIStateTraverse(false, -INT_MAX, id);
			md->SetROIStateTraverse(true, id, id);
		}
		sub_groups = true;
	}
	break;
	}
	if (!sub_groups)
		for (int i = 0; i < m_tree->topLevelItemCount(); i++)
			HideOtherDatasetsTraversal(m_tree->topLevelItem(i), item, false);
	UpdateTreeIcons();
	RefreshViews();
}

void TreePanel::HideOtherVolumes(QTreeWidgetItem* item)
{
	if (!CurCanvas()) return;
	if (!item) item = m_tree->currentItem();
	if (!item) return;
	const int type = ItemType(item);
	if (type != TypeVolume && type != TypeMesh && type != TypeAnnotations)
		return;
	PushVisHistory();
	for (int i = 0; i < m_tree->topLevelItemCount(); i++)
		HideOtherDatasetsTraversal(m_tree->topLevelItem(i), item, true);
	UpdateTreeIcons();
	RefreshViews();
}

void TreePanel::HideSelectedItem()
{
	if (m_fixed || !CurCanvas()) return;
	QTreeWidgetItem* sel_item = m_tree->currentItem();
	if (!sel_item) return;
	PushVisHistory();
	switch (ItemType(sel_item))
	{
	case TypeVolume:
		if (VolumeData* vd = VolumeOf(sel_item)) vd->SetDisp(false);
		break;
	case TypeMesh:
		if (MeshData* md = MeshOf(sel_item)) md->SetDisp(false);
		break;
	case TypeSegment:
	case TypeSegGroup:
	{
		VolumeData* vd = VolumeOf(ParentVolItem(sel_item));
		if (vd)
		{
			const std::wstring name = sel_item->text(0).toStdWString();
			if (vd->GetROIid(name) == -1)
				return;
			vd->SetROISel(name, false);
		}
	}
	break;
	case TypeMeshSeg:
	case TypeMeshSegGroup:
		if (MeshData* md = MeshOf(ParentMeshItem(sel_item)))
			md->SetROIState(sel_item->data(0, ROLE_ID).toInt(), false);
		break;
	}
	UpdateTreeIcons();
	RefreshViews();
}
