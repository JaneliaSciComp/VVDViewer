#include "AnnotationPropPanel.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QLineEdit>
#include <QLabel>
#include <QColorDialog>
#include <QSignalBlocker>
#include <QDoubleValidator>
#include <QIntValidator>
#include <algorithm>

#include "DataManager.h"

using FLIVR::Color;

namespace
{
	// wx: a wxTextCtrl of 400 x 100. QPlainTextEdit's own size hint is 192 px
	// tall, which made this the tallest Properties page, and the dock is
	// fitted to the tallest page (the render views lost that height for every
	// selection). It still stretches into the space the dock has.
	class MemoEdit : public QPlainTextEdit
	{
	public:
		using QPlainTextEdit::QPlainTextEdit;
		QSize sizeHint() const override { return QSize(QPlainTextEdit::sizeHint().width(), minimumHeight()); }
	};
}

AnnotationPropPanel::AnnotationPropPanel(QWidget* parent)
	: QWidget(parent)
{
	setStyleSheet("QWidget { font-size: 11px; } QLineEdit, QPushButton { min-height: 18px; }");

	// ---- sizer_v1: Memo
	auto* sizer_v1 = new QVBoxLayout();
	sizer_v1->setSpacing(2);
	auto* sizer_1 = new QHBoxLayout();
	sizer_1->addSpacing(10);
	sizer_1->addWidget(new QLabel(tr("Memo:"), this));
	sizer_1->addStretch(1);
	auto* sizer_2 = new QHBoxLayout();
	sizer_2->addSpacing(10);
	m_memo_text = new MemoEdit(this);
	m_memo_text->setObjectName("MemoText");
	m_memo_text->setMinimumSize(400, 100);
	sizer_2->addWidget(m_memo_text, 1);
	auto* sizer_3 = new QHBoxLayout();
	sizer_3->addSpacing(330);
	m_memo_update_btn = new QPushButton(tr("Update"), this);
	m_memo_update_btn->setObjectName("MemoUpdateBtn");
	sizer_3->addWidget(m_memo_update_btn);
	sizer_3->addStretch(1);
	sizer_v1->addLayout(sizer_1);
	sizer_v1->addLayout(sizer_2, 1);
	sizer_v1->addLayout(sizer_3);

	// ---- sizer_v2: Transparency, Min Score, Number of Particles, Color
	auto* sizer_v2 = new QVBoxLayout();
	sizer_v2->setSpacing(0);
	auto label = [this](const QString& t, int w) {
		auto* l = new QLabel(t, this);
		l->setFixedWidth(w);
		return l;
	};
	auto* sizer_4 = new QHBoxLayout();
	sizer_4->addSpacing(20);
	sizer_4->addWidget(label(tr(" Transparency: "), 100));
	m_alpha_sldr = new QSlider(Qt::Horizontal, this);
	m_alpha_sldr->setObjectName("AlphaSldr");
	m_alpha_sldr->setRange(0, 255);
	m_alpha_sldr->setValue(255);
	m_alpha_sldr->setFixedWidth(200);
	m_alpha_text = new QLineEdit("1.00", this);
	m_alpha_text->setObjectName("AlphaText");
	m_alpha_text->setFixedWidth(50);
	{
		auto* v = new QDoubleValidator(0.0, 1e9, 2, m_alpha_text);   // wx vald_fp2
		v->setNotation(QDoubleValidator::StandardNotation);
		m_alpha_text->setValidator(v);
	}
	sizer_4->addWidget(m_alpha_sldr);
	sizer_4->addWidget(m_alpha_text);
	sizer_4->addStretch(1);

	auto* sizer_5 = new QHBoxLayout();
	sizer_5->addSpacing(20);
	sizer_5->addWidget(label(tr(" Min Score: "), 100));
	m_th_sldr = new QSlider(Qt::Horizontal, this);
	m_th_sldr->setObjectName("ThSldr");
	m_th_sldr->setRange(0, 65535);
	m_th_sldr->setFixedWidth(200);
	m_th_text = new QLineEdit("0", this);
	m_th_text->setObjectName("ThText");
	m_th_text->setFixedWidth(50);
	m_th_text->setValidator(new QIntValidator(0, 2147483647, m_th_text));   // wx vald_int (unsigned)
	sizer_5->addWidget(m_th_sldr);
	sizer_5->addWidget(m_th_text);
	sizer_5->addStretch(1);

	m_num_particles = new QLabel(tr("        Number of Particles:  "), this);
	m_num_particles->setObjectName("NumParticles");
	m_num_particles->setMinimumWidth(250);

	auto* sizer_6 = new QHBoxLayout();
	sizer_6->addWidget(label(tr(" Color: "), 110));
	m_diff_picker = new QPushButton(this);
	m_diff_picker->setObjectName("DiffPicker");
	m_diff_picker->setFixedSize(180, 30);
	SetPickerColor(Qt::white);
	sizer_6->addWidget(m_diff_picker);
	sizer_6->addStretch(1);

	sizer_v2->addSpacing(5);
	sizer_v2->addLayout(sizer_4);
	sizer_v2->addSpacing(5);
	sizer_v2->addLayout(sizer_5);
	sizer_v2->addSpacing(5);
	sizer_v2->addWidget(m_num_particles);
	sizer_v2->addSpacing(5);
	sizer_v2->addLayout(sizer_6);
	sizer_v2->addStretch(1);

	auto* sizer_all = new QHBoxLayout(this);
	sizer_all->setContentsMargins(4, 4, 4, 4);
	sizer_all->addLayout(sizer_v1, 0);
	sizer_all->addLayout(sizer_v2, 0);
	sizer_all->addStretch(1);

	connect(m_memo_update_btn, &QPushButton::clicked, this, &AnnotationPropPanel::OnMemoUpdateBtn);
	// wx EVT_COMMAND_SCROLL -> the text box; EVT_TEXT (any change of the box,
	// also the one made by the slider) -> apply
	connect(m_alpha_sldr, &QSlider::valueChanged, this, &AnnotationPropPanel::OnAlphaChange);
	connect(m_alpha_text, &QLineEdit::textChanged, this, [this]() { if (!m_updating) OnAlphaText(); });
	connect(m_th_sldr, &QSlider::valueChanged, this, &AnnotationPropPanel::OnThresholdChange);
	connect(m_th_text, &QLineEdit::textChanged, this, [this]() { if (!m_updating) OnThresholdText(); });
	connect(m_diff_picker, &QPushButton::clicked, this, &AnnotationPropPanel::OnDiffButton);
}

void AnnotationPropPanel::SetPickerColor(const QColor& c)
{
	m_diff_picker->setProperty("color", c);
	m_diff_picker->setStyleSheet(QString("background-color: %1; border: 1px solid #808080;").arg(c.name()));
}

void AnnotationPropPanel::SetAnnotations(Annotations* ann)
{
	m_ann = ann;
	GetSettings();
}

// APropView::GetSettings
void AnnotationPropPanel::GetSettings()
{
	if (!m_ann)
		return;

	m_updating = true;
	m_memo_text->setPlainText(QString::fromStdString(m_ann->GetMemo()));
	if (m_ann->GetMemoRO())
	{
		m_memo_text->setReadOnly(true);
		m_memo_update_btn->setEnabled(false);
	}
	else
	{
		m_memo_text->setReadOnly(false);
		m_memo_update_btn->setEnabled(true);
	}

	if (MeshData* md = m_ann->GetMesh())
	{
		Color amb, diff, spec;
		double shine, alpha;
		md->GetMaterial(amb, diff, spec, shine, alpha);

		// wx: wxColor(diff * 255) (truncated), written back to the mesh
		QColor c(int(diff.r() * 255), int(diff.g() * 255), int(diff.b() * 255));
		SetPickerColor(c);
		Color color(c.red() / 255.0, c.green() / 255.0, c.blue() / 255.0);
		md->SetColor(color, MESH_COLOR_DIFF);
		amb = color * 0.3;
		md->SetColor(amb, MESH_COLOR_AMB);

		//alpha
		alpha = m_ann->GetAlpha();
		m_alpha_sldr->setValue(int(alpha * 255));
		m_alpha_text->setText(QString::asprintf("%.2f", alpha));   // wx ChangeValue

		//threshold
		m_th_sldr->setMaximum((int)(m_ann->GetMaxScore() + 1.0));
		double th = m_ann->GetThreshold();
		m_th_sldr->setValue(int(th + 0.5));
		m_th_text->setText(QString::asprintf("%d", (int)(th + 0.5)));

		m_diff_picker->show();
		m_alpha_sldr->show();
		m_alpha_text->show();
		m_th_sldr->show();
		m_th_text->show();
		m_num_particles->show();

		m_updating = false;
		OnThresholdText();
	}
	else
	{
		m_diff_picker->hide();
		m_alpha_sldr->hide();
		m_alpha_text->hide();
		m_th_sldr->hide();
		m_th_text->hide();
		m_num_particles->hide();
		m_updating = false;
	}
}

void AnnotationPropPanel::OnMemoUpdateBtn()
{
	if (m_ann)
	{
		std::string str = m_memo_text->toPlainText().toStdString();
		m_ann->SetMemo(str);
	}
}

void AnnotationPropPanel::OnAlphaChange(int v)
{
	if (m_updating) return;
	double val = (double)v / 255.0;
	m_alpha_text->setText(QString::asprintf("%.2f", val));   // -> OnAlphaText (wx SetValue)
}

void AnnotationPropPanel::OnAlphaText()
{
	double alpha = m_alpha_text->text().toDouble();
	{
		QSignalBlocker b(m_alpha_sldr);
		m_alpha_sldr->setValue(int(alpha * 255.0 + 0.5));
	}
	if (m_ann)
	{
		m_ann->SetAlpha(alpha);
		Q_EMIT annotationsChanged(false);
	}
}

void AnnotationPropPanel::OnThresholdChange(int v)
{
	if (m_updating) return;
	double val = (double)v;
	m_th_text->setText(QString::asprintf("%d", (int)(val + 0.5)));   // -> OnThresholdText
}

// APropView::OnThresholdText. Deliberate difference: wx counts the particles
// (MeasureDlg::UpdateList + GetCount) before it applies the new threshold, so
// the label showed the count of the previous value; here the threshold is
// set first and the label matches the list.
void AnnotationPropPanel::OnThresholdText()
{
	double th = m_th_text->text().toDouble();
	{
		QSignalBlocker b(m_th_sldr);
		m_th_sldr->setValue(int(th + 0.5));
	}

	long count = 0;
	if (m_ann)
	{
		m_ann->SetThreshold(th);
		if (m_count)
			count = m_count(m_ann);
		m_num_particles->setText(QString::asprintf("       Number of Particles:  %ld", count));
		Q_EMIT annotationsChanged(false);
	}
}

void AnnotationPropPanel::OnDiffButton()
{
	QColor init = m_diff_picker->property("color").value<QColor>();
	QColor c = QColorDialog::getColor(init, this, tr("Annotation Color"));
	if (!c.isValid())
		return;
	ApplyColor(c);
}

// APropView::OnDiffChange
void AnnotationPropPanel::ApplyColor(const QColor& c)
{
	SetPickerColor(c);
	Color color(c.red() / 255.0, c.green() / 255.0, c.blue() / 255.0);
	if (m_ann && m_ann->GetMesh())
	{
		m_ann->GetMesh()->SetColor(color, MESH_COLOR_DIFF);
		Color amb = color * 0.3;
		m_ann->GetMesh()->SetColor(amb, MESH_COLOR_AMB);
		Q_EMIT annotationsChanged(true);
	}
}
