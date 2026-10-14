#include "LegendMenu.h"
#include "RenderCanvas.h"

#include <QListWidget>
#include <QWidgetAction>
#include <QScrollBar>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QScreen>
#include <QGuiApplication>
#include <algorithm>

#include "DataManager.h"

namespace
{
	enum { TypeVolume = 0, TypeMesh = 1 };
	constexpr int PtrRole = Qt::UserRole;
	constexpr int TypeRole = Qt::UserRole + 1;
}

LegendMenu::LegendMenu(RenderCanvas* canvas, QWidget* parent)
	: QMenu(parent), m_canvas(canvas)
{
	m_show_all = addAction(tr("Show All"));
	m_hide_all = addAction(tr("Hide All"));
	addSeparator();
	m_empty = addAction(tr("(no volumes or meshes)"));
	m_empty->setEnabled(false);

	m_list = new QListWidget(this);
	m_list->setSelectionMode(QAbstractItemView::NoSelection);
	m_list->setFocusPolicy(Qt::NoFocus);
	m_list->setFrameShape(QFrame::NoFrame);
	m_list->setUniformItemSizes(true);
	m_list->setTextElideMode(Qt::ElideMiddle);
	m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
	m_list->setMouseTracking(true);
	// blend into the menu; hover row like a menu item
	m_list->setStyleSheet(
		// padding: check boxes under the text of the menu items above
		"QListWidget { background: transparent; border: none; padding-left: 6px; }"
		"QListWidget::item { padding: 1px 6px 1px 2px; }"
		"QListWidget::item:hover { background: palette(highlight); color: palette(highlighted-text); }");
	m_list_action = new QWidgetAction(this);
	m_list_action->setDefaultWidget(m_list);
	addAction(m_list_action);

	connect(m_list, &QListWidget::itemClicked, this, &LegendMenu::OnItemClicked);
	connect(m_show_all, &QAction::triggered, this, [this]() { SetAll(true); });
	connect(m_hide_all, &QAction::triggered, this, [this]() { SetAll(false); });
	connect(this, &QMenu::aboutToShow, this, &LegendMenu::Reload);
}

void LegendMenu::Reload()
{
	const int scroll = m_list->verticalScrollBar()->value();
	const int old_count = m_list->count();
	m_list->clear();
	if (m_canvas)
	{
		auto add = [this](const QString& name, void* ptr, int type, bool legend) {
			auto* item = new QListWidgetItem(name, m_list);
			// not user-checkable: the whole row toggles on click (wx
			// OnLeftDown); a checkable item would toggle twice when the box
			// itself is hit
			item->setFlags(Qt::ItemIsEnabled);
			item->setCheckState(legend ? Qt::Checked : Qt::Unchecked);
			item->setData(PtrRole, QVariant::fromValue(ptr));
			item->setData(TypeRole, type);
			item->setToolTip(name);
		};
		// wx: GetDispVolumeData / GetMeshData (displayed, group visibility honoured)
		for (VolumeData* vd : m_canvas->PopVolumeList())
			if (vd)
				add(QString::fromStdWString(vd->GetName()), vd, TypeVolume, vd->GetLegend());
		for (MeshData* md : m_canvas->GetPopMeshList())
			if (md)
				add(QString::fromStdWString(md->GetName()), md, TypeMesh, md->GetLegend());
	}
	const bool any = m_list->count() > 0;
	m_show_all->setEnabled(any);
	m_hide_all->setEnabled(any);
	m_empty->setVisible(!any);
	const QSize old_size = m_list->size();
	FitList();
	m_list->verticalScrollBar()->setValue(scroll);
	// QMenu caches the geometry of its actions (QMenuPrivate::actionRects)
	// and recomputes it only on an action change, not when a widget action's
	// widget changes size: without this the menu kept the height of its
	// first popup and rows added since (a volume loaded later) were cut off.
	// Reload runs in aboutToShow too, before the menu is visible.
	if (old_count != m_list->count() || old_size != m_list->size() || m_list_action->isVisible() != any)
	{
		m_list_action->setVisible(false);
		m_list_action->setVisible(any);
		if (isVisible())
			adjustSize();
	}
}

// every row visible up to ~60% of the screen height, then a scroll bar
void LegendMenu::FitList()
{
	const int n = m_list->count();
	if (n <= 0) { m_list->setFixedSize(0, 0); return; }
	QScreen* scr = parentWidget() ? parentWidget()->screen() : QGuiApplication::primaryScreen();
	const QRect avail = scr ? scr->availableGeometry() : QRect(0, 0, 1280, 800);
	const int rh = std::max(1, m_list->sizeHintForRow(0));
	const int maxh = std::max(rh * 5, int(avail.height() * 0.6));
	const int h = std::min(n * rh, maxh - maxh % rh);
	const bool scrolls = n * rh > h;
	int w = m_list->sizeHintForColumn(0) + 14;
	if (scrolls) w += m_list->verticalScrollBar()->sizeHint().width();
	w = std::clamp(w, 120, std::max(120, avail.width() / 2));
	m_list->setVerticalScrollBarPolicy(scrolls ? Qt::ScrollBarAlwaysOn : Qt::ScrollBarAlwaysOff);
	m_list->setFixedSize(w, h);
}

// Show All / Hide All act without closing the menu (rows of the list never
// close it: their clicks go to the list widget)
void LegendMenu::mouseReleaseEvent(QMouseEvent* e)
{
	QAction* a = actionAt(e->position().toPoint());
	if (KeepsOpen(a))
	{
		if (a->isEnabled()) a->trigger();
		e->accept();
		return;
	}
	QMenu::mouseReleaseEvent(e);
}

void LegendMenu::keyPressEvent(QKeyEvent* e)
{
	const int k = e->key();
	if ((k == Qt::Key_Return || k == Qt::Key_Enter || k == Qt::Key_Space) && KeepsOpen(activeAction()))
	{
		if (activeAction()->isEnabled()) activeAction()->trigger();
		e->accept();
		return;
	}
	QMenu::keyPressEvent(e);
}

// wx OnItemChecked / OnItemUnchecked
void LegendMenu::OnItemClicked(QListWidgetItem* item)
{
	if (!item || !m_canvas) return;
	const bool on = item->checkState() != Qt::Checked;
	if (Apply(item, on))
		m_canvas->RefreshOverlayChange();   // overlay only (wx RefreshGL)
}

void LegendMenu::SetAll(bool on)
{
	if (!m_canvas) return;
	bool changed = false;
	for (int i = 0; i < m_list->count(); ++i)
		changed = Apply(m_list->item(i), on) || changed;
	if (changed)
		m_canvas->RefreshOverlayChange();
}

// the pointer is checked against the canvas lists since the menu may be stale
bool LegendMenu::Apply(QListWidgetItem* item, bool on)
{
	if (!item) return false;
	item->setCheckState(on ? Qt::Checked : Qt::Unchecked);
	void* ptr = item->data(PtrRole).value<void*>();
	if (item->data(TypeRole).toInt() == TypeVolume)
	{
		const auto& vl = m_canvas->Volumes();
		auto it = std::find(vl.begin(), vl.end(), static_cast<VolumeData*>(ptr));
		if (it == vl.end() || !*it) return false;
		if ((*it)->GetLegend() == on) return false;
		(*it)->SetLegend(on);
	}
	else
	{
		const auto& ml = m_canvas->Meshes();
		auto it = std::find(ml.begin(), ml.end(), static_cast<MeshData*>(ptr));
		if (it == ml.end() || !*it) return false;
		if ((*it)->GetLegend() == on) return false;
		(*it)->SetLegend(on);
	}
	return true;
}
