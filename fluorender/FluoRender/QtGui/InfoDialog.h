#ifndef VVD_QT_INFODIALOG_H
#define VVD_QT_INFODIALOG_H

// "Information": Qt port of VAnnoView (wx), opened by Analyze > "Infomation...".
// An "Info:" label over a multi-line text box holding the current volume's
// reader info string (BaseReader::GetInfo()). Edits are written back with
// BaseReader::SetInfo() when the data changes, when the box loses focus and
// when the dialog is hidden (wx: SaveInfo() on SetData()).

#include <QDialog>

class QPlainTextEdit;
class TreeLayer;
class VolumeData;
class AnalysisContext;

class InfoDialog : public QDialog
{
	Q_OBJECT
public:
	explicit InfoDialog(AnalysisContext* ctx, QWidget* parent = nullptr);

	// wx VAnnoView::SetData(): saves the previous layer's text, then shows the
	// info of `data` (volumes only; meshes are blank as in wx). Call on tree
	// selection changes. nullptr clears.
	void SetData(TreeLayer* data);
	void SetVolume(VolumeData* vd);
	void SaveInfo();
	QString GetText() const;
	// keyboard shortcuts of the main window should be suppressed while true
	// (wx: VRenderFrame::SetKeyLock from myTextCtrl focus events)
	bool IsEditing() const;

Q_SIGNALS:
	void editingChanged(bool editing);

protected:
	void showEvent(QShowEvent* e) override;
	void hideEvent(QHideEvent* e) override;
	bool eventFilter(QObject* obj, QEvent* ev) override;

private:
	AnalysisContext* m_ctx;
	TreeLayer* m_data;
	QPlainTextEdit* m_text;
};

#endif
