#ifndef VVD_QT_PANESCROLLAREA_H
#define VVD_QT_PANESCROLLAREA_H

// A frameless scroll area around a pane's content (dock panels, the render
// view's control rows, plugin panes) so that the pane can be made smaller
// than its content's minimum size: below it, scroll bars appear; at or above
// it the content fills the area exactly as without the scroll area (same
// size hint, no scroll bars). Directions not in `scroll` keep the content's
// minimum (plus the scroll bar's height / width when the other direction
// needs a non-overlay scroll bar), so the content is never cut there.

#include <QEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QTimer>

class PaneScrollArea : public QScrollArea
{
public:
	PaneScrollArea(QWidget* content, Qt::Orientations scroll, QSize min_size, QWidget* parent = nullptr)
		: QScrollArea(parent), m_scroll(scroll), m_min(min_size)
	{
		setObjectName(content->objectName().isEmpty() ? QString("PaneScrollArea") : content->objectName() + "_scroll");
		setFrameShape(QFrame::NoFrame);
		setWidgetResizable(true);
		setHorizontalScrollBarPolicy((scroll & Qt::Horizontal) ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
		setVerticalScrollBarPolicy((scroll & Qt::Vertical) ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
		setSizePolicy(content->sizePolicy());
		const bool fill = content->autoFillBackground();
		setWidget(content);
		// QScrollArea::setWidget turns autoFillBackground on; the content
		// used to be drawn on its parent's background
		content->setAutoFillBackground(fill);
		viewport()->setAutoFillBackground(false);
		content->installEventFilter(this);
	}

	// the content's effective minimum (what a layout would enforce)
	static QSize ContentMin(const QWidget* c)
	{
		if (!c) return QSize(0, 0);
		const QSize ms = c->minimumSize(), mh = c->minimumSizeHint();
		QSize m(ms.width() > 0 ? ms.width() : mh.width(), ms.height() > 0 ? ms.height() : mh.height());
		return m.expandedTo(QSize(0, 0)).boundedTo(c->maximumSize());
	}

	QSize sizeHint() const override
	{
		const int f = 2 * frameWidth();
		QSize s = widget() ? widget()->sizeHint().expandedTo(ContentMin(widget())) : QSize(0, 0);
		return s + QSize(f, f);
	}

	QSize minimumSizeHint() const override
	{
		const int f = 2 * frameWidth();
		const QSize cm = ContentMin(widget());
		int w = cm.width(), h = cm.height();
		if (m_scroll & Qt::Horizontal) w = qMin(w, m_min.width());
		else if (m_need_v) w += verticalScrollBar()->sizeHint().width();
		if (m_scroll & Qt::Vertical) h = qMin(h, m_min.height());
		else if (m_need_h) h += horizontalScrollBar()->sizeHint().height();
		return QSize(w + f, h + f);
	}

protected:
	bool eventFilter(QObject* o, QEvent* e) override
	{
		if (o == widget() && e->type() == QEvent::LayoutRequest)
			QTimer::singleShot(0, this, [this]() { UpdateNeed(); updateGeometry(); });
		return QScrollArea::eventFilter(o, e);
	}

	void resizeEvent(QResizeEvent* e) override
	{
		QScrollArea::resizeEvent(e);
		UpdateNeed();
	}

private:
	void UpdateNeed()
	{
		const QSize cm = ContentMin(widget());
		const int f = 2 * frameWidth();
		// overlay (transient) scroll bars take no room
		const bool th = style()->styleHint(QStyle::SH_ScrollBar_Transient, nullptr, horizontalScrollBar());
		const bool tv = style()->styleHint(QStyle::SH_ScrollBar_Transient, nullptr, verticalScrollBar());
		const bool nh = !th && (m_scroll & Qt::Horizontal) && width() - f < cm.width();
		const bool nv = !tv && (m_scroll & Qt::Vertical) && height() - f < cm.height();
		if (nh != m_need_h || nv != m_need_v)
		{
			m_need_h = nh;
			m_need_v = nv;
			updateGeometry();
		}
	}

	Qt::Orientations m_scroll;
	QSize m_min;
	bool m_need_h = false;
	bool m_need_v = false;
};

// a dock's panel, through its PaneScrollArea if it has one
#include <QDockWidget>
inline QWidget* DockContent(const QDockWidget* d)
{
	QWidget* w = d ? d->widget() : nullptr;
	if (auto* sa = dynamic_cast<PaneScrollArea*>(w)) return sa->widget();
	return w;
}

#endif
