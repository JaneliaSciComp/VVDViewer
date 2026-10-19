// MainWindow: wx VRenderFrame UI management and the render view's
// right-click menu.
//
//   * Show/Hide UI (wx ID_ShowHideUI, m_tb_menu_ui, ToggleAllTools,
//     OnShowHideView, OnPaneClose): the toolbar button toggles all panes
//     (hiding remembers which were shown, showing brings exactly those back,
//     all of them when nothing is remembered); its drop-down arrow holds
//     "Toggle All Panels" and one check item per pane. The Windows menu has
//     the same pane items (wx m_top_window); every check mark follows the
//     real visibility of its pane (closing a pane with its title bar button
//     included).
//   * Windows menu: Show/Hide Toolbar, Show/Hide UI, Datasets, the panes,
//     Open New VVDViewer, Full Screen (not on macOS, as wx).
//   * File > Open URL (wx OnOpenURL).
//   * /ui_layout of projects (wx SaveProject / OpenProject).
//   * wx VRenderVulkanView::OnContextMenu with its handlers (Show all, Show
//     only selected, Hide selected, Join / Split selected segments, Undo /
//     Redo visibility; the fragment entries for volumes in NA mode) and
//     PickMesh.

#include "MainWindow.h"
#include <QDateTime>
#include "MoviePanel.h"

#include <QAction>
#include <QMenu>
#include <QToolBar>
#include <QDockWidget>
#include <QTimer>
#include <QEvent>
#include <QInputDialog>
#include <QLocale>
#include <QCursor>

#include "RenderCanvas.h"
#include "TreePanel.h"
#include "MeasureDialog.h"
#include "SettingsDialog.h"
#include "DataManager.h"
#include "Formats/brkxml_reader.h"

// ---------------------------------------------------------------------------
// panes
// ---------------------------------------------------------------------------

void MainWindow::MakeFloatingTool(QWidget* w)
{
	if (!w) return;
	// macOS hides tool windows while the app is inactive; wx floating panes stay
	w->setAttribute(Qt::WA_MacAlwaysShowToolWindow, true);
	if ((w->windowFlags() & Qt::WindowType_Mask) == Qt::Tool) return;
	const bool vis = w->isVisible();
	const QRect geo = w->geometry();
	w->setWindowFlag(Qt::Tool, true);
	if (vis) { w->setGeometry(geo); w->show(); }
}

MeasureDialog* MainWindow::EnsureMeasureDialog()
{
	if (!m_measure_dlg)
	{
		m_measure_dlg = new MeasureDialog(m_ruler_tool, this, m_settings, this);
		MakeFloatingTool(m_measure_dlg);   // stays above the view while picking points
		m_measure_dlg->installEventFilter(this);   // check marks follow show / hide
	}
	return m_measure_dlg;
}

bool MainWindow::eventFilter(QObject* obj, QEvent* e)
{
	if (obj && obj == m_measure_dlg && (e->type() == QEvent::Show || e->type() == QEvent::Hide))
		QTimer::singleShot(0, this, [this]() { SyncUIChecks(); });
	// Record/Export's height need changes with its tab: fit right away
	if (obj && obj == m_movie && e->type() == QEvent::LayoutRequest && !(m_dock_fit_timer && m_dock_fit_timer->isActive()))
		QTimer::singleShot(0, this, [this]() { FitDocksToContent(); });
	// a fitted dock (Properties, Record/Export) changing height while the
	// window keeps its size and no fit is pending: its separator was dragged,
	// so FitDocksToContent leaves it alone for the session
	if (obj && (obj == m_prop_dock || obj == m_movie_dock) && e->type() == QEvent::Resize)
	{
		auto* d = static_cast<QDockWidget*>(obj);
		const QVariant fit = d->property("vvdFitH");
		const bool settling = (m_dock_fit_timer && m_dock_fit_timer->isActive()) ||
			QDateTime::currentMSecsSinceEpoch() - m_dock_fit_ms < 500;
		if (!fit.isValid())
			;
		else if (settling || size() != m_dock_fit_win)
			d->setProperty("vvdFitH", d->height());   // the fit settling, or the window resizing
		else if (d->height() != fit.toInt())
			d->setProperty("vvdHandSized", true);
	}
	return QMainWindow::eventFilter(obj, e);
}

bool MainWindow::IsPaneShown(int pane) const
{
	switch (pane)
	{
	case PaneTree: return m_tree_dock && !m_tree_dock->isHidden();
	case PaneMeasure: return m_measure_dlg && m_measure_dlg->isVisible();
	case PaneMovie: return m_movie_dock && !m_movie_dock->isHidden();
	case PaneAdjust: return m_adjust_dock && !m_adjust_dock->isHidden();
	case PaneClip: return m_clip_dock && !m_clip_dock->isHidden();
	case PaneProp: return m_prop_dock && !m_prop_dock->isHidden();
	}
	return false;
}

void MainWindow::ShowPane(int pane, bool show)
{
	QDockWidget* dock = nullptr;
	switch (pane)
	{
	case PaneTree: dock = m_tree_dock; break;
	case PaneMovie: dock = m_movie_dock; break;
	case PaneAdjust: dock = m_adjust_dock; break;
	case PaneClip: dock = m_clip_dock; break;
	case PaneProp: dock = m_prop_dock; break;
	case PaneMeasure:
		if (show)
		{
			// wx ShowMeasureDlg (the pane is not dockable: a floating window)
			EnsureMeasureDialog()->GetSettings();
			ShowTool(m_measure_dlg);
		}
		else if (m_measure_dlg)
			m_measure_dlg->hide();
		break;
	}
	if (dock) dock->setVisible(show);
	SyncUIChecks();
}

void MainWindow::SyncUIChecks()
{
	if (m_syncing_ui) return;
	m_syncing_ui = true;
	for (int i = 0; i < PaneCount; i++)
	{
		const bool on = IsPaneShown(i);
		if (m_ui_pane_act[i]) m_ui_pane_act[i]->setChecked(on);
		if (m_win_pane_act[i]) m_win_pane_act[i]->setChecked(on);
	}
	if (m_ui_toolbar_act && m_main_tb) m_ui_toolbar_act->setChecked(!m_main_tb->isHidden());
	if (m_fullscreen_act) m_fullscreen_act->setChecked(isFullScreen());
	m_syncing_ui = false;
}

// wx VRenderFrame::ToggleAllTools
void MainWindow::ToggleAllTools()
{
	bool any = false;
	for (int i = 0; i < PaneCount; i++) any |= IsPaneShown(i);
	m_ui_state = any;

	if (m_ui_state)
	{
		m_ui_state_cache.clear();
		for (int i = 0; i < PaneCount; i++) m_ui_state_cache[i] = IsPaneShown(i);
		//hide all
		for (int i = 0; i < PaneCount; i++) ShowPane(i, false);
		m_ui_state = false;
	}
	else
	{
		if (m_ui_state_cache.empty())
		{
			//show all
			for (int i = 0; i < PaneCount; i++) ShowPane(i, true);
		}
		else
		{
			for (int i = 0; i < PaneCount; i++)
				if (m_ui_state_cache[i]) ShowPane(i, true);
		}
		m_ui_state = true;
	}
	SyncUIChecks();
}

// wx OnShowHideView: toggle one pane
void MainWindow::OnShowHideView(int pane)
{
	ShowPane(pane, !IsPaneShown(pane));
}

// wx OnShowHideUI (Windows menu "Show/Hide UI" and the toolbar button
// itself; the button's arrow opens m_tb_menu_ui). wx on macOS pops
// m_tb_menu_ui up instead -- here the arrow (and the Windows menu items)
// give the per-pane toggles, the button toggles all as the other builds do.
void MainWindow::OnShowHideUI()
{
#ifdef __APPLE__
	m_tb_menu_ui->popup(QCursor::pos());   // wx _DARWIN: PopupMenu(m_tb_menu_ui)
#else
	ToggleAllTools();
#endif
}

void MainWindow::BuildUIMenu()
{
	m_tb_menu_ui = new QMenu(this);
	QAction* all = m_tb_menu_ui->addAction(tr("Toggle All Panels"), this, &MainWindow::ToggleAllTools);
	all->setStatusTip(tr("Show/hide all panels"));
	// wx UITEXT_* captions and help strings, wx m_tb_menu_ui order
	struct Item { int pane; const char* text; const char* help; };
	const Item items[] = {
		{ PaneTree, "Workspace", "Show/hide the workspace panel" },
		{ PaneMeasure, "Measurement", "Show/hide the measurement panel" },
		{ PaneMovie, "Record/Export", "Show/hide the movie panel" },
		{ PaneAdjust, "Output Adjustments", "Show/hide the output adjustment panel" },
		{ PaneClip, "Clipping Planes", "Show/hide the clipping plane control panel" },
		{ PaneProp, "Properties", "Show/hide the property panel" },
	};
	for (const Item& it : items)
	{
		QAction* a = m_tb_menu_ui->addAction(tr(it.text));
		a->setCheckable(true);
		a->setStatusTip(tr(it.help));
		const int pane = it.pane;
		connect(a, &QAction::triggered, this, [this, pane]() { if (!m_syncing_ui) OnShowHideView(pane); });
		m_ui_pane_act[pane] = a;
	}
	// wx OnPaneClose: the title bar close buttons uncheck the items
	for (QDockWidget* d : { m_tree_dock, m_movie_dock, m_adjust_dock, m_clip_dock, m_prop_dock })
		if (d) connect(d, &QDockWidget::visibilityChanged, this, [this]() { QTimer::singleShot(0, this, [this]() { SyncUIChecks(); }); });
}

// ---------------------------------------------------------------------------
// wx OnOpenURL
// ---------------------------------------------------------------------------

void MainWindow::OnOpenURL()
{
	bool ok = false;
	QString url = QInputDialog::getText(this, tr("URL Input"), tr("Enter URL:"), QLineEdit::Normal, QString(), &ok);
	if (!ok) return;
	QString extension;
	const int slash = url.lastIndexOf('/');
	if (slash >= 0)
	{
		const QString file = url.mid(slash + 1);
		const int dot = file.lastIndexOf('.');
		if (dot >= 0) extension = file.mid(dot + 1);
	}
	if (extension.isEmpty())
	{
		// a zarr / n5 folder: its attribute file
		QString normalized = url;
		while (normalized.endsWith('/')) normalized.chop(1);
		normalized += "/";
		std::string zarr = (normalized + ".zattrs").toStdString();
		if (BRKXMLReader::DownloadFile(zarr))
			url = normalized + ".zattrs";
		else
		{
			std::string n5 = (normalized + "attributes.json").toStdString();
			if (BRKXMLReader::DownloadFile(n5))
				url = normalized + "attributes.json";
		}
	}
	LoadVolumes(QStringList(url));
}

// ---------------------------------------------------------------------------
// wx SaveProject / OpenProject: /ui_layout
// ---------------------------------------------------------------------------

namespace
{
	const char* kUILayout = "/ui_layout/";
}

void MainWindow::StoreProjectUILayout()
{
	auto put = [this](const QString& key, bool v) {
		const QString full = QString(kUILayout) + key;
		for (auto& kv : m_prj_passthrough)
			if (kv.first == full) { kv.second = v ? "1" : "0"; return; }
		m_prj_passthrough.emplace_back(full, v ? "1" : "0");
	};
	// the keys wx writes (wx writes no ui_movie_view and always
	// ui_measure_view = false)
	put("ui_main_tb", m_main_tb && !m_main_tb->isHidden());
	put("ui_tree_view", IsPaneShown(PaneTree));
	put("ui_measure_view", false);
	put("ui_adjust_view", IsPaneShown(PaneAdjust));
	put("ui_clip_view", IsPaneShown(PaneClip));
	put("ui_prop_view", IsPaneShown(PaneProp));
	put("ui_main_tb_float", m_main_tb && m_main_tb->isFloating());
	put("ui_tree_view_float", m_tree_dock && m_tree_dock->isFloating());
	put("ui_measure_view_float", true);   // wx: a Dockable(false) pane, always floating
	put("ui_adjust_view_float", m_adjust_dock && m_adjust_dock->isFloating());
	put("ui_clip_view_float", m_clip_dock && m_clip_dock->isFloating());
	put("ui_prop_view_float", m_prop_dock && m_prop_dock->isFloating());
}

void MainWindow::ApplyProjectUILayout()
{
	auto find = [this](const QString& key, bool& v) {
		const QString full = QString(kUILayout) + key;
		for (const auto& kv : m_prj_passthrough)
			if (kv.first == full)
			{
				bool ok = false;
				const int i = kv.second.trimmed().toInt(&ok);
				if (ok) { v = i != 0; return true; }
			}
		return false;
	};
	bool b = false, fl = false;
	if (find("ui_main_tb", b) && m_main_tb) m_main_tb->setVisible(b);
	struct P { const char* key; int pane; QDockWidget* dock; };
	const P panes[] = {
		{ "ui_tree_view", PaneTree, m_tree_dock },
		{ "ui_measure_view", PaneMeasure, nullptr },
		{ "ui_movie_view", PaneMovie, m_movie_dock },
		{ "ui_adjust_view", PaneAdjust, m_adjust_dock },
		{ "ui_clip_view", PaneClip, m_clip_dock },
		{ "ui_prop_view", PaneProp, m_prop_dock },
	};
	for (const P& p : panes)
	{
		if (!find(p.key, b)) continue;
		ShowPane(p.pane, b);
		if (b && p.dock && find(QString(p.key) + "_float", fl))
			p.dock->setFloating(fl);
	}
	SyncUIChecks();
}

// ---------------------------------------------------------------------------
// wx VRenderVulkanView::OnContextMenu
// ---------------------------------------------------------------------------

QMenu* MainWindow::BuildViewContextMenu()
{
	bool na_mode = false;
	for (VolumeData* vd : m_canvas->PopVolumeList())
		if (vd && vd->GetDisp() && vd->GetNAMode()) { na_mode = true; break; }

	auto* menu = new QMenu(this);
	if (na_mode)
	{
		menu->addAction(tr("Show all"), this, &MainWindow::OnShowAllFragments);
		menu->addAction(tr("Deselect all"), this, &MainWindow::OnDeselectAllFragments);
		menu->addSeparator();
		menu->addAction(tr("Show only selected"), this, &MainWindow::OnHideOtherFragments);
		menu->addAction(tr("Hide selected"), this, &MainWindow::OnHideSelectedFragments);
	}
	else
	{
		menu->addAction(tr("Show all"), this, [this]() { m_tree->ShowAllDatasets(); });
		menu->addSeparator();
		menu->addAction(tr("Show only selected"), this, [this]() { m_tree->HideOtherDatasets(); });
		menu->addAction(tr("Hide selected"), this, [this]() { m_tree->HideSelectedItem(); });
		menu->addSeparator();
		menu->addAction(tr("Join selected segments"), this, &MainWindow::OnJoinSegments);
		menu->addAction(tr("Split selected segments"), this, &MainWindow::OnSplitSegments);
	}
	menu->addSeparator();
	menu->addAction(tr("Undo visibility"), this, [this]() { m_tree->UndoVisibility(); });
	menu->addAction(tr("Redo visibility"), this, [this]() { m_tree->RedoVisibility(); });
	return menu;
}

void MainWindow::ShowViewContextMenu(const QPoint& pos)
{
	if (m_view_ctx_menu)
	{
		m_view_ctx_menu->hide();
		m_view_ctx_menu->deleteLater();
	}
	m_view_ctx_menu = BuildViewContextMenu();
	m_view_ctx_menu->setAttribute(Qt::WA_DeleteOnClose);
	connect(m_view_ctx_menu, &QObject::destroyed, this, [this](QObject* o) { if (o == m_view_ctx_menu) m_view_ctx_menu = nullptr; });
	m_view_ctx_menu->popup(m_canvas->mapToGlobal(pos));
}

// the volumes in NA mode of the view's layers (wx: m_layer_list; groups that
// are hidden are skipped when shown_groups_only)
std::vector<VolumeData*> MainWindow::NAVolumes(bool shown_groups_only)
{
	std::vector<VolumeData*> na_vols;
	for (int i = 0; i < m_canvas->GetLayerNum(); i++)
	{
		TreeLayer* layer = m_canvas->GetLayer(i);
		if (!layer) continue;
		switch (layer->IsA())
		{
		case 2://volume data
		{
			VolumeData* vd = (VolumeData*)layer;
			if (vd->GetNAMode()) na_vols.push_back(vd);
		}
		break;
		case 5://group
		{
			DataGroup* group = (DataGroup*)layer;
			if (shown_groups_only && !group->GetDisp()) continue;
			for (int j = 0; j < group->GetVolumeNum(); j++)
			{
				VolumeData* vd = group->GetVolumeData(j);
				if (vd && vd->GetNAMode()) na_vols.push_back(vd);
			}
		}
		break;
		}
	}
	return na_vols;
}

namespace
{
	// wx OnShowAllFragments / OnDeselectAllFragments / OnHideOtherFragments /
	// OnHideSelectedFragments: segment masks (0 hidden, 1 shown, 2 selected)
	// with value `from` become `to`; one visibility history step
	bool ChangeFragments(const std::vector<VolumeData*>& vols, int from, int to, bool skip_zero, TreePanel* tree)
	{
		bool changed = false;
		for (VolumeData* vd : vols)
		{
			std::set<int>* ids = vd->GetActiveSegIDs();
			if (!ids) continue;
			for (int id : *ids)
			{
				if (vd->GetSegmentMask(id) == from && !(skip_zero && id == 0))
				{
					if (!changed)
					{
						tree->PushVisHistory();
						changed = true;
					}
					vd->SetSegmentMask(id, to);
				}
			}
		}
		return changed;
	}
}

void MainWindow::OnShowAllFragments()
{
	if (ChangeFragments(NAVolumes(true), 0, 1, true, m_tree))
	{
		m_canvas->RefreshView();
		m_tree->UpdateTreeIcons();
	}
}

void MainWindow::OnDeselectAllFragments()
{
	if (ChangeFragments(NAVolumes(true), 2, 1, true, m_tree))
	{
		m_canvas->RefreshView();
		m_tree->UpdateTreeIcons();
	}
}

void MainWindow::OnHideOtherFragments()
{
	if (ChangeFragments(NAVolumes(false), 1, 0, false, m_tree))
	{
		m_canvas->RefreshView();
		m_tree->UpdateTreeIcons();
	}
}

void MainWindow::OnHideSelectedFragments()
{
	if (ChangeFragments(NAVolumes(false), 2, 0, false, m_tree))
	{
		m_canvas->RefreshView();
		m_tree->UpdateTreeIcons();
	}
}

// wx OnJoinSegments / OnSplitSegments: the current volume in index mode
void MainWindow::OnJoinSegments()
{
	VolumeData* vd = m_canvas->GetCurrentVolume();
	if (vd && vd->GetColormapMode() == 3)
	{
		vd->CombineSelectedROIs();
		RefreshTree();   // wx UpdateROITree(m_cur_vol)
		m_canvas->RefreshView();
	}
}

void MainWindow::OnSplitSegments()
{
	VolumeData* vd = m_canvas->GetCurrentVolume();
	if (vd && vd->GetColormapMode() == 3)
	{
		vd->SplitSelectedROIs();
		RefreshTree();
		m_canvas->RefreshView();
	}
}

// wx VRenderVulkanView::PickMesh (the selection part)
void MainWindow::PickMeshAt(const QPoint& pos)
{
	int seg = -1;
	MeshData* md = m_canvas->PickMesh(pos, &seg);
	if (md)
	{
		if (md->isTree())
			m_tree->SelectROI(md, seg);
		else
			m_tree->SelectMesh(md);
	}
	else if (m_tree->GetCurrentSelType() == TreePanel::TypeMesh)
		m_tree->SelectByName(tr("Render View:1"), TreePanel::TypeView);   // wx Select(view, "")
}
