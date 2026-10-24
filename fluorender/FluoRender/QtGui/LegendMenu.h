#ifndef VVD_QT_LEGENDMENU_H
#define VVD_QT_LEGENDMENU_H

// Drop-down menu of the render view's "Legend" button (replaces the floating
// LegendDialog, the Qt port of wx LegendPanel / LegendListCtrl). One check
// row per displayed volume (wx GetDispVolumeData) and per mesh (wx
// GetMeshData); the check state is VolumeData/MeshData::SetLegend, i.e.
// whether the name appears in the legend overlay. Clicking a row toggles it
// (wx OnLeftDown) and the menu stays open, so several names can be ticked in
// a row; "Show All" / "Hide All" on top do not close it either. The rows
// live in a scrolling list whose height is capped, for projects with
// hundreds of volumes.

#include <QMenu>
#include <QPointer>

class QListWidget;
class QListWidgetItem;
class QWidgetAction;
class RenderCanvas;

class LegendMenu : public QMenu
{
	Q_OBJECT
public:
	LegendMenu(RenderCanvas* canvas, QWidget* parent = nullptr);

	// rebuild the rows from the canvas (keeps the scroll position)
	void Reload();

	// testing (VVD_QT_GRAB_LEGEND)
	QListWidget* List() const { return m_list; }
	QAction* ShowAllAction() const { return m_show_all; }
	QAction* HideAllAction() const { return m_hide_all; }

protected:
	void mouseReleaseEvent(QMouseEvent* e) override;
	void keyPressEvent(QKeyEvent* e) override;

private:
	void OnItemClicked(QListWidgetItem* item);
	void SetAll(bool on);
	bool Apply(QListWidgetItem* item, bool on);
	void FitList();
	bool KeepsOpen(QAction* a) const { return a && (a == m_show_all || a == m_hide_all); }

	QPointer<RenderCanvas> m_canvas;
	QAction* m_show_all = nullptr;
	QAction* m_hide_all = nullptr;
	QAction* m_empty = nullptr;
	QWidgetAction* m_list_action = nullptr;
	QListWidget* m_list = nullptr;
};

#endif // VVD_QT_LEGENDMENU_H
