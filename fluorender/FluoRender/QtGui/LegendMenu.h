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
// hundreds of volumes. "Names on Objects" (not in wx) puts the volumes'
// names on their cell bodies in the view instead of the list at the bottom
// (ViewOverlays::SetNamesOnObjects); it keeps the menu open too. Below it
// the names' placement: "Name Position: Auto" (laid out in every frame) or
// "Name Position: Manual (drag names)" (each name kept where it is, dragged
// in the view to move it: ViewOverlays::SetNamesManual), and "Reset Name
// Positions" (laid out automatically again).

#include <QMenu>
#include <QPointer>

class QListWidget;
class QListWidgetItem;
class QWidgetAction;
class RenderCanvas;
class ViewOverlays;

class LegendMenu : public QMenu
{
	Q_OBJECT
public:
	LegendMenu(RenderCanvas* canvas, QWidget* parent = nullptr);

	// rebuild the rows from the canvas (keeps the scroll position)
	void Reload();
	// the overlays of the view, for "Names on Objects"
	void SetOverlays(ViewOverlays* ov);

	// left padding of a row: its check mark, then the text where the style's
	// check box used to put it
	static constexpr int CheckPad = 31;

	// testing (VVD_QT_GRAB_LEGEND)
	QListWidget* List() const { return m_list; }
	QAction* ShowAllAction() const { return m_show_all; }
	QAction* HideAllAction() const { return m_hide_all; }
	QAction* NamesOnObjectsAction() const { return m_on_objects; }
	QAction* NamesAutoAction() const { return m_pos_auto; }
	QAction* NamesManualAction() const { return m_pos_manual; }
	QAction* ResetNamesAction() const { return m_pos_reset; }

protected:
	void mouseReleaseEvent(QMouseEvent* e) override;
	void keyPressEvent(QKeyEvent* e) override;

private:
	void OnItemClicked(QListWidgetItem* item);
	void SetAll(bool on);
	bool Apply(QListWidgetItem* item, bool on);
	void FitList();
	void SyncNameActions();
	bool KeepsOpen(QAction* a) const { return a && (a == m_show_all || a == m_hide_all || a == m_on_objects); }

	QPointer<RenderCanvas> m_canvas;
	QPointer<ViewOverlays> m_overlays;
	QAction* m_show_all = nullptr;
	QAction* m_hide_all = nullptr;
	QAction* m_on_objects = nullptr;
	QAction* m_pos_auto = nullptr;
	QAction* m_pos_manual = nullptr;
	QAction* m_pos_reset = nullptr;
	QAction* m_empty = nullptr;
	QWidgetAction* m_list_action = nullptr;
	QListWidget* m_list = nullptr;
};

#endif // VVD_QT_LEGENDMENU_H
