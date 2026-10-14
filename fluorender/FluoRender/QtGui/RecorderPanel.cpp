#include "RecorderPanel.h"
#include "MoviePanel.h"
#include "RenderCanvas.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIntValidator>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPixmap>
#include <QPushButton>
#include <QScrollBar>
#include <QVBoxLayout>

#include "Animator/Interpolator.h"
#include "DataManager.h"
#include "img/key.xpm"

namespace
{
	double ToDouble(const QString& s, bool* ok = nullptr)
	{
		bool k = false;
		double v = s.trimmed().toDouble(&k);
		if (ok) *ok = k;
		return k ? v : 0.0;
	}
}

// ---------------------------------------------------------------------------
// KeyList (wx KeyListCtrl)
// ---------------------------------------------------------------------------

KeyList::KeyList(QWidget* parent) : QTreeWidget(parent)
{
	setColumnCount(5);
	setHeaderLabels(QStringList() << "ID" << "Frame" << "Inbetweens" << "Interpolation" << "Description");
	setColumnWidth(0, 40);
	setColumnWidth(1, 60);
	setColumnWidth(2, 80);
	setColumnWidth(3, 80);
	setColumnWidth(4, 80);
	setRootIsDecorated(false);
	setSelectionMode(QAbstractItemView::SingleSelection);
	setUniformRowHeights(true);
	setIconSize(QSize(16, 16));
	header()->setStretchLastSection(true);
	header()->setSectionsMovable(false);

	connect(this, &QTreeWidget::itemSelectionChanged, this, &KeyList::OnSelection);
	connect(this, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem*, int) { OnAct(); });
	// wx OnScroll: scrolling ends editing without an update
	connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int) { EndEdit(false); });
	connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, [this](int) { EndEdit(false); });
}

void KeyList::Append(int id, int time, int duration, int interp, const std::string& description)
{
	static const QIcon key_icon{ QPixmap(key_xpm) };
	auto* it = new QTreeWidgetItem(QStringList()
		<< QString::number(id) << QString::number(time) << QString::number(duration)
		<< (interp == 0 ? "Linear" : "Smooth") << QString::fromStdString(description));
	it->setIcon(0, key_icon);
	addTopLevelItem(it);
}

int KeyList::SelectedRow() const
{
	const QList<QTreeWidgetItem*> sel = selectedItems();
	if (sel.isEmpty()) return -1;
	return indexOfTopLevelItem(sel.first());
}

void KeyList::SelectRow(int row)
{
	if (row < 0 || row >= topLevelItemCount()) return;
	setCurrentItem(topLevelItem(row));
	topLevelItem(row)->setSelected(true);
}

int KeyList::KeyIndexOfRow(int row) const
{
	if (!m_interpolator || row < 0 || row >= topLevelItemCount()) return -1;
	const int id = topLevelItem(row)->text(0).toInt();
	return m_interpolator->GetKeyIndex(id);
}

void KeyList::DeleteSel()
{
	const int item = SelectedRow();
	if (item == -1 || !m_interpolator)
		return;
	const int id = topLevelItem(item)->text(0).toInt();
	m_interpolator->RemoveKey(id);
	UpdateList();
}

void KeyList::DeleteAll()
{
	if (!m_interpolator)
		return;
	m_interpolator->Clear();
	UpdateList();
}

void KeyList::UpdateList()
{
	EndEdit(false);
	if (!m_interpolator || !m_movie)
		return;

	{
		QSignalBlocker b(this);
		clear();
		for (int i = 0; i < m_interpolator->GetKeyNum(); i++)
		{
			int id = m_interpolator->GetKeyID(i);
			int time = (int)m_interpolator->GetKeyTime(i);
			int duration = (int)m_interpolator->GetKeyDuration(i);
			int interp = m_interpolator->GetKeyType(i);
			std::string desc = m_interpolator->GetKeyDesc(i);
			Append(id, time, duration, interp, desc);
		}
	}

	long fps = m_movie->GetFPS();
	int frames = int(m_interpolator->GetLastT());
	if (frames > 0 && fps > 0)
	{
		double runtime = (double)frames / (double)fps;
		m_movie->SetMovieTime(runtime);
	}
}

void KeyList::UpdateText()
{
	if (!m_interpolator || !m_movie)
		return;
	for (int i = 0; i < m_interpolator->GetKeyNum() && i < topLevelItemCount(); i++)
	{
		QTreeWidgetItem* it = topLevelItem(i);
		it->setText(0, QString::number(m_interpolator->GetKeyID(i)));
		it->setText(1, QString::number((int)m_interpolator->GetKeyTime(i)));
		it->setText(2, QString::number((int)m_interpolator->GetKeyDuration(i)));
		it->setText(3, m_interpolator->GetKeyType(i) == 0 ? "Linear" : "Smooth");
		it->setText(4, QString::fromStdString(m_interpolator->GetKeyDesc(i)));
	}

	long fps = m_movie->GetFPS();
	int frames = int(m_interpolator->GetLastT());
	if (frames > 0 && fps > 0)
	{
		double runtime = (double)frames / (double)fps;
		m_movie->SetMovieTime(runtime);
	}
}

// wx OnSelection: editors over the selected row (the frame editor stays hidden)
void KeyList::OnSelection()
{
	const int item = SelectedRow();
	if (item == -1)
	{
		EndEdit();   // wx OnEndSelection
		return;
	}
	if (item != m_editing_item)
		EndEdit();
	if (m_dragging_to_item == -1)
		BeginEdit(item);
	m_editing_item = item;
}

void KeyList::BeginEdit(int row)
{
	QTreeWidgetItem* it = topLevelItem(row);
	if (!it) return;
	m_editing_item = row;

	//duration edit
	m_duration_text = new QLineEdit(it->text(2));
	m_duration_text->setValidator(new QIntValidator(0, 1000000000, m_duration_text));
	m_duration_text->setFrame(false);
	connect(m_duration_text, &QLineEdit::textChanged, this, [this](const QString& str) {   // wx OnDurationText
		if (m_editing_item == -1 || !m_interpolator) return;
		int index = KeyIndexOfRow(m_editing_item);
		bool ok = false;
		double duration = ToDouble(str, &ok);
		if (ok)
		{
			m_interpolator->ChangeDuration(index, duration);
			if (QTreeWidgetItem* e = topLevelItem(m_editing_item)) e->setText(2, str);
		}
	});
	setItemWidget(it, 2, m_duration_text);

	//interpolation combo box
	m_interpolation_cmb = new QComboBox;
	m_interpolation_cmb->addItems(QStringList() << "Linear" << "Smooth");
	m_interpolation_cmb->setCurrentIndex(it->text(3) == "Smooth" ? 1 : 0);
	connect(m_interpolation_cmb, QOverload<int>::of(&QComboBox::activated), this, [this](int sel) {   // wx OnInterpoCmb
		if (m_editing_item == -1 || !m_interpolator) return;
		int index = KeyIndexOfRow(m_editing_item);
		FlKeyGroup* keygroup = m_interpolator->GetKeyGroup(index);
		if (keygroup)
		{
			keygroup->type = sel;
			if (QTreeWidgetItem* e = topLevelItem(m_editing_item)) e->setText(3, sel == 0 ? "Linear" : "Smooth");
		}
	});
	setItemWidget(it, 3, m_interpolation_cmb);

	//description edit
	m_description_text = new QLineEdit(it->text(4));
	m_description_text->setFrame(false);
	connect(m_description_text, &QLineEdit::textChanged, this, [this](const QString& str) {   // wx OnDescritionText
		if (m_editing_item == -1 || !m_interpolator) return;
		int index = KeyIndexOfRow(m_editing_item);
		FlKeyGroup* keygroup = m_interpolator->GetKeyGroup(index);
		if (keygroup)
		{
			keygroup->desc = str.toStdString();
			if (QTreeWidgetItem* e = topLevelItem(m_editing_item)) e->setText(4, str);
		}
	});
	setItemWidget(it, 4, m_description_text);
}

void KeyList::EndEdit(bool update)
{
	const bool shown = m_duration_text != nullptr;
	if (m_editing_item >= 0 && m_editing_item < topLevelItemCount())
	{
		QTreeWidgetItem* it = topLevelItem(m_editing_item);
		removeItemWidget(it, 2);
		removeItemWidget(it, 3);
		removeItemWidget(it, 4);
	}
	m_duration_text = nullptr;
	m_interpolation_cmb = nullptr;
	m_description_text = nullptr;
	m_editing_item = -1;
	if (shown && update) UpdateText();
}

// wx OnAct: show the state at the key's time
void KeyList::OnAct()
{
	const int item = SelectedRow();
	if (item == -1 || !m_interpolator || !m_movie)
		return;
	int index = KeyIndexOfRow(item);
	double time = m_interpolator->GetKeyTime(index);
	double end_frame = m_interpolator->GetLastT();
	if (end_frame <= 0.0) end_frame = 1.0;
	m_movie->SetProgress(time / end_frame);
	m_movie->SetRendering(time / end_frame);
}

void KeyList::keyPressEvent(QKeyEvent* e)
{
	if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace)
	{
		DeleteSel();
		return;
	}
	QTreeWidget::keyPressEvent(e);
}

// wx OnBeginDrag / OnDragging / OnEndDrag: dragging the selected row moves
// its key in the interpolator while the mouse passes over other rows
void KeyList::mousePressEvent(QMouseEvent* e)
{
	QTreeWidget::mousePressEvent(e);
	m_press_pos = e->pos();
	QTreeWidgetItem* it = itemAt(e->pos());
	m_press_on_selected = it && it->isSelected() && (e->button() == Qt::LeftButton);
	m_dragging = false;
}

void KeyList::mouseMoveEvent(QMouseEvent* e)
{
	if (!(e->buttons() & Qt::LeftButton) || !m_press_on_selected || !m_interpolator)
	{
		QTreeWidget::mouseMoveEvent(e);
		return;
	}
	if (!m_dragging)
	{
		if ((e->pos() - m_press_pos).manhattanLength() < QApplication::startDragDistance())
			return;
		const int sel = SelectedRow();
		if (sel == -1) return;
		// wx OnBeginDrag
		EndEdit(false);
		m_editing_item = sel;
		m_dragging = true;
		m_dragging_to_item = -1;
		setCursor(Qt::WaitCursor);
	}
	QTreeWidgetItem* over = itemAt(e->pos());
	const int index = over ? indexOfTopLevelItem(over) : -1;
	if (index >= 0 && index != m_editing_item && index != m_dragging_to_item)
	{
		m_dragging_to_item = index;
		//change the content in the interpolator
		if (m_editing_item > m_dragging_to_item)
			m_interpolator->MoveKeyBefore(m_editing_item, m_dragging_to_item);
		else
			m_interpolator->MoveKeyAfter(m_editing_item, m_dragging_to_item);
		UpdateText();
		m_editing_item = m_dragging_to_item;
		QSignalBlocker b(this);
		SelectRow(m_editing_item);
	}
}

void KeyList::mouseReleaseEvent(QMouseEvent* e)
{
	if (m_dragging)
	{
		unsetCursor();
		m_dragging = false;
		m_dragging_to_item = -1;
		m_press_on_selected = false;
		const int sel = SelectedRow();
		m_editing_item = -1;
		if (sel >= 0) BeginEdit(sel);
		return;
	}
	m_press_on_selected = false;
	QTreeWidget::mouseReleaseEvent(e);
}

// ---------------------------------------------------------------------------
// RecorderPanel (wx RecorderDlg)
// ---------------------------------------------------------------------------

RecorderPanel::RecorderPanel(QWidget* parent) : QWidget(parent)
{
	QVBoxLayout* sizerV = new QVBoxLayout(this);
	sizerV->setContentsMargins(0, 0, 0, 0);
	sizerV->setSpacing(5);

	//list
	sizerV->addWidget(new QLabel("Key Frames:"));
	m_keylist = new KeyList(this);
	m_keylist->setMinimumHeight(60);
	sizerV->addWidget(m_keylist, 1);

	//default duration
	QHBoxLayout* group3 = new QHBoxLayout;
	group3->setSpacing(5);
	QLabel* st = new QLabel("Default:");
	st->setMinimumWidth(50);
	m_duration_text = new QLineEdit("30");
	m_duration_text->setValidator(new QIntValidator(0, 1000000000, m_duration_text));
	m_duration_text->setFixedWidth(34);
	m_interpolation_cmb = new QComboBox;
	m_interpolation_cmb->addItems(QStringList() << "Linear" << "Smooth");
	m_interpolation_cmb->setCurrentIndex(0);
	m_interpolation_cmb->setMinimumContentsLength(6);
	m_interpolation_cmb->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);

	//key buttons
	m_set_key_btn = new QPushButton("Add");
	m_del_key_btn = new QPushButton("Delete");
	m_del_all_btn = new QPushButton("Del. All");
	m_set_key_btn->setMinimumWidth(50);
	m_del_key_btn->setMinimumWidth(55);
	m_del_all_btn->setMinimumWidth(60);
	for (QPushButton* b : { m_set_key_btn, m_del_key_btn, m_del_all_btn })
		b->setFocusPolicy(Qt::NoFocus);

	group3->addWidget(st);
	group3->addWidget(m_duration_text);
	group3->addWidget(m_interpolation_cmb);
	group3->addStretch(1);
	group3->addWidget(m_set_key_btn);
	group3->addWidget(m_del_key_btn);
	group3->addWidget(m_del_all_btn);
	sizerV->addLayout(group3);

	//recoding
	m_vol_record_chk = new QCheckBox("Record volume properties");
	m_vol_record_chk->setChecked(false);
	sizerV->addWidget(m_vol_record_chk);

	connect(m_set_key_btn, &QPushButton::clicked, this, &RecorderPanel::AddKey);   // wx ID_SetKeyBtn -> OnInsKey
	connect(m_del_key_btn, &QPushButton::clicked, this, &RecorderPanel::DeleteKey);
	connect(m_del_all_btn, &QPushButton::clicked, this, &RecorderPanel::DeleteAll);
}

void RecorderPanel::SetContext(RenderCanvas* canvas, Interpolator* ip, MoviePanel* movie)
{
	m_canvas = canvas;
	m_interpolator = ip;
	m_movie = movie;
	m_keylist->SetContext(ip, movie);
}

void RecorderPanel::SetSelection(int index)
{
	// wx: only when a row is selected already
	const int item = m_keylist->SelectedRow();
	if (index != item && item != -1)
		m_keylist->SelectRow(index);
}

// wx OnInsKey (InsertKey -> VRenderView::AddKeyFrame)
void RecorderPanel::AddKey()
{
	if (!m_interpolator || !m_canvas)
		return;
	const int item = m_keylist->SelectedRow();
	double duration = ToDouble(m_duration_text->text());
	int interpolation = m_interpolation_cmb->currentIndex();
	m_canvas->AddKeyFrame(m_interpolator, duration, interpolation, m_vol_record_chk->isChecked());

	m_keylist->UpdateList();
	if (item >= 0)
		m_keylist->SelectRow(item);
}

bool RecorderPanel::MoveOne(std::vector<bool>& chan_mask, int lv)
{
	int i;
	int cur_lv = 0;
	int lv_pos = -1;
	for (i = (int)chan_mask.size() - 1; i >= 0; i--)
	{
		if (chan_mask[i])
		{
			cur_lv++;
			if (cur_lv == lv)
			{
				lv_pos = i;
				break;
			}
		}
	}
	if (lv_pos >= 0)
	{
		if (lv_pos == (int)chan_mask.size() - lv)
			return MoveOne(chan_mask, ++lv);
		else
		{
			if (!chan_mask[lv_pos + 1])
			{
				for (i = lv_pos; i < (int)chan_mask.size(); i++)
				{
					if (i == lv_pos)
						chan_mask[i] = false;
					else if (i <= lv_pos + lv)
						chan_mask[i] = true;
					else
						chan_mask[i] = false;
				}
				return true;
			}
			else return false;//no space anymore
		}
	}
	else return false;
}

void RecorderPanel::AutoKeyChanComb(int comb)
{
	if (!m_canvas || !m_interpolator)
		return;

	double duration = ToDouble(m_duration_text->text());

	FLKeyCode keycode;
	double t = m_interpolator->GetLastT();
	t = t < 0.0 ? 0.0 : t;
	if (t > 0.0) t += duration;

	const std::vector<VolumeData*>& vols = m_canvas->Volumes();   // wx GetAllVolumeData
	int numChan = (int)vols.size();
	std::vector<bool> chan_mask;
	//initiate mask
	for (int i = 0; i < numChan; i++)
		chan_mask.push_back(i < comb);

	const std::string vname = m_canvas->GetViewName().toStdString();
	do
	{
		m_interpolator->Begin(t);

		//for all volumes
		for (int i = 0; i < numChan; i++)
		{
			VolumeData* vd = vols[i];
			keycode.l0 = 1;
			keycode.l0_name = vname;
			keycode.l1 = 2;
			keycode.l1_name = vd->GetName().ToStdString();
			//display only
			keycode.l2 = 0;
			keycode.l2_name = "display";
			m_interpolator->AddKey(new FlKeyBoolean(keycode, chan_mask[i]));
		}

		m_interpolator->End();
		t += duration;
	} while (GetMask(chan_mask));

	m_keylist->UpdateList();
}
