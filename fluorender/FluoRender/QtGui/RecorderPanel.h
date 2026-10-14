#ifndef VVD_QT_RECORDERPANEL_H
#define VVD_QT_RECORDERPANEL_H

// "Advanced" page of the Record/Export pane: Qt port of the wx RecorderDlg
// (keyframe recorder) and its KeyListCtrl.
//
//   wx                                   here
//   KeyListCtrl (wxListCtrl report)      KeyList (QTreeWidget): ID / Frame /
//                                        Inbetweens / Interpolation / Description,
//                                        key icon (key.xpm)
//   in-place editors on the selected     the same three editors as item widgets
//     row (duration, interpolation,      of the selected row; changes go to the
//     description; frame hidden)         Interpolator while typing (EVT_TEXT)
//   OnAct (activate)                     jump to the key (SetProgress/SetRendering)
//   drag a row (OnBeginDrag/OnDragging)  the row moves while dragged over others
//                                        (MoveKeyBefore / MoveKeyAfter)
//   Delete / Backspace                   delete the selected key
//   scrolling                            ends editing
//   "Default:" duration + interpolation  same; "Add" (OnInsKey -> AddKeyFrame),
//   Add / Delete / Del. All              "Delete", "Del. All"
//   "Record volume properties"           same (AddKeyFrame record_volume_params)
//   AutoKeyChanComb                      same (Auto Key page "Generate")
//
// The wx RecorderDlg also has Play/Save/Stop/Rewind entry points
// (SetParamCapture) used only by scripts; the pane's own footer plays and
// saves the keyframe movie (VMovieView, page 1), as in wx.

#include <QWidget>
#include <QTreeWidget>
#include <vector>

class QLineEdit;
class QComboBox;
class QPushButton;
class QCheckBox;
class RenderCanvas;
class Interpolator;
class MoviePanel;

class KeyList : public QTreeWidget
{
	Q_OBJECT
public:
	explicit KeyList(QWidget* parent = nullptr);

	void SetContext(Interpolator* ip, MoviePanel* movie) { m_interpolator = ip; m_movie = movie; }
	void Append(int id, int time, int duration, int interp, const std::string& description);
	void DeleteSel();
	void DeleteAll();
	void UpdateList();    // wx KeyListCtrl::Update
	void UpdateText();    // wx KeyListCtrl::UpdateText
	int SelectedRow() const;
	void SelectRow(int row);
	void EndEdit(bool update = true);

protected:
	void keyPressEvent(QKeyEvent* e) override;
	void mousePressEvent(QMouseEvent* e) override;
	void mouseMoveEvent(QMouseEvent* e) override;
	void mouseReleaseEvent(QMouseEvent* e) override;

private:
	void OnSelection();
	void OnAct();
	void BeginEdit(int row);
	int KeyIndexOfRow(int row) const;

	Interpolator* m_interpolator = nullptr;
	MoviePanel* m_movie = nullptr;
	int m_editing_item = -1;
	int m_dragging_to_item = -1;
	bool m_dragging = false;
	QPoint m_press_pos;
	bool m_press_on_selected = false;
	QLineEdit* m_duration_text = nullptr;
	QComboBox* m_interpolation_cmb = nullptr;
	QLineEdit* m_description_text = nullptr;
};

class RecorderPanel : public QWidget
{
	Q_OBJECT
public:
	explicit RecorderPanel(QWidget* parent = nullptr);

	void SetContext(RenderCanvas* canvas, Interpolator* ip, MoviePanel* movie);
	void UpdateList() { m_keylist->UpdateList(); }
	void SetSelection(int index);          // wx RecorderDlg::SetSelection
	void AutoKeyChanComb(int comb);        // wx RecorderDlg::AutoKeyChanComb
	// wx OnInsKey ("Add"): a key frame of the current state after the last one
	void AddKey();
	void DeleteKey() { m_keylist->DeleteSel(); }
	void DeleteAll() { m_keylist->DeleteAll(); }
	KeyList* GetKeyList() const { return m_keylist; }
	QLineEdit* DurationText() const { return m_duration_text; }
	QComboBox* InterpolationCombo() const { return m_interpolation_cmb; }
	QCheckBox* VolRecordCheck() const { return m_vol_record_chk; }

private:
	bool MoveOne(std::vector<bool>& chan_mask, int lv);
	bool GetMask(std::vector<bool>& chan_mask) { return MoveOne(chan_mask, 1); }

	RenderCanvas* m_canvas = nullptr;
	Interpolator* m_interpolator = nullptr;
	MoviePanel* m_movie = nullptr;
	KeyList* m_keylist = nullptr;
	QLineEdit* m_duration_text = nullptr;
	QComboBox* m_interpolation_cmb = nullptr;
	QPushButton* m_set_key_btn = nullptr;
	QPushButton* m_del_key_btn = nullptr;
	QPushButton* m_del_all_btn = nullptr;
	QCheckBox* m_vol_record_chk = nullptr;
};

#endif // VVD_QT_RECORDERPANEL_H
