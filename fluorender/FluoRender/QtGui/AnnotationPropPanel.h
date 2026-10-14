#ifndef VVD_QT_ANNOTATIONPROPPANEL_H
#define VVD_QT_ANNOTATIONPROPPANEL_H

// Annotation "Properties" pane: Qt port of APropView (APropView.cpp).
//
//   left : "Memo:" + multi-line memo + Update (read-only when GetMemoRO)
//   right: Transparency (0..255 -> 0.00..1.00), Min Score (0..max score + 1),
//          "Number of Particles:", Color (the annotation mesh's diffuse
//          colour, ambient = 0.3 x diffuse)
// The right column is shown only for annotations that carry a mesh.

#include <QWidget>
#include <functional>

class QPlainTextEdit;
class QPushButton;
class QSlider;
class QLineEdit;
class QLabel;
class Annotations;

class AnnotationPropPanel : public QWidget
{
	Q_OBJECT
public:
	explicit AnnotationPropPanel(QWidget* parent = nullptr);

	// wx SetAnnotations(ann, vrv) -> GetSettings
	void SetAnnotations(Annotations* ann);
	Annotations* GetAnnotations() const { return m_ann; }
	void GetSettings();
	// wx OnThresholdText: MeasureDlg::UpdateList + GetCount(m_ann)
	void SetCountProvider(std::function<long(Annotations*)> f) { m_count = std::move(f); }

	// testing aids (the widgets the wx handlers read)
	QPlainTextEdit* MemoText() const { return m_memo_text; }
	QPushButton* MemoUpdateButton() const { return m_memo_update_btn; }
	QSlider* AlphaSlider() const { return m_alpha_sldr; }
	QLineEdit* AlphaText() const { return m_alpha_text; }
	QSlider* ThresholdSlider() const { return m_th_sldr; }
	QLineEdit* ThresholdText() const { return m_th_text; }
	QLabel* NumParticlesLabel() const { return m_num_particles; }
	QPushButton* ColorButton() const { return m_diff_picker; }
	void ApplyColor(const QColor& c);   // wx OnDiffChange

Q_SIGNALS:
	// wx RefreshVRenderViews(tree)
	void annotationsChanged(bool tree);

private:
	void OnMemoUpdateBtn();
	void OnAlphaChange(int v);
	void OnAlphaText();
	void OnThresholdChange(int v);
	void OnThresholdText();
	void OnDiffButton();
	void SetPickerColor(const QColor& c);

	Annotations* m_ann = nullptr;
	std::function<long(Annotations*)> m_count;
	bool m_updating = false;

	QPlainTextEdit* m_memo_text = nullptr;
	QPushButton* m_memo_update_btn = nullptr;
	QWidget* m_right = nullptr;
	QSlider* m_alpha_sldr = nullptr;
	QLineEdit* m_alpha_text = nullptr;
	QSlider* m_th_sldr = nullptr;
	QLineEdit* m_th_text = nullptr;
	QLabel* m_num_particles = nullptr;
	QPushButton* m_diff_picker = nullptr;
	QWidget* m_alpha_row = nullptr;
	QWidget* m_th_row = nullptr;
	QWidget* m_color_row = nullptr;
};

#endif // VVD_QT_ANNOTATIONPROPPANEL_H
