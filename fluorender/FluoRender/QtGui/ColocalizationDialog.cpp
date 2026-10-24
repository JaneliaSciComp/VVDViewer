#include "ColocalizationDialog.h"
#include "AnalysisContext.h"

#include "DataManager.h"
#include "VolumeSelector.h"
#include "VolumeCalculator.h"

#include <QBoxLayout>
#include <QCheckBox>
#include <QCompleter>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QStringListModel>

namespace
{
	QLabel* FixedLabel(const QString& text, QWidget* parent)
	{
		auto* l = new QLabel(text, parent);
		l->setFixedWidth(75);
		return l;
	}
}

ColocalizationDialog::ColocalizationDialog(AnalysisContext* ctx, QWidget* parent) :
	QDialog(parent),
	m_ctx(ctx),
	m_vol_a(nullptr),
	m_vol_b(nullptr)
{
	setWindowTitle(tr("Colocalization Analysis"));
	setObjectName("ColocalizationDlg");
	setModal(false);
	resize(400, 165);

	m_names = new QStringListModel(this);

	auto make_row = [](QVBoxLayout* v) {
		auto* h = new QHBoxLayout;
		h->setContentsMargins(10, 0, 10, 0);
		h->setSpacing(4);
		v->addLayout(h);
		return h;
	};

	auto* sizerV = new QVBoxLayout(this);
	sizerV->setContentsMargins(0, 10, 0, 10);
	sizerV->setSpacing(10);

	//operand A
	QHBoxLayout* sizer_1 = make_row(sizerV);
	m_calc_load_a_btn = new QPushButton(tr("Load"), this);
	m_calc_load_a_btn->setFixedWidth(75);
	m_calc_a_text = new QLineEdit(this);
	m_calc_a_text->setMinimumWidth(200);
	m_calc_a_text->setCompleter(new QCompleter(m_names, m_calc_a_text));
	sizer_1->addWidget(FixedLabel(tr("Volume A:"), this));
	sizer_1->addWidget(m_calc_load_a_btn);
	sizer_1->addWidget(m_calc_a_text, 1);

	//operand B
	QHBoxLayout* sizer_2 = make_row(sizerV);
	m_calc_load_b_btn = new QPushButton(tr("Load"), this);
	m_calc_load_b_btn->setFixedWidth(75);
	m_calc_b_text = new QLineEdit(this);
	m_calc_b_text->setMinimumWidth(75);
	m_calc_b_text->setCompleter(new QCompleter(m_names, m_calc_b_text));
	sizer_2->addWidget(FixedLabel(tr("Volume B:"), this));
	sizer_2->addWidget(m_calc_load_b_btn);
	sizer_2->addWidget(m_calc_b_text, 1);

	//min size
	QHBoxLayout* sizer_3 = make_row(sizerV);
	m_min_size_sldr = new QSlider(Qt::Horizontal, this);
	m_min_size_sldr->setRange(0, 100);
	m_min_size_sldr->setValue(10);
	m_min_size_text = new QLineEdit(QStringLiteral("10"), this);
	m_min_size_text->setFixedWidth(75);
	m_min_size_text->setValidator(new QIntValidator(0, 2147483647, m_min_size_text));
	sizer_3->addWidget(FixedLabel(tr("Min Size:"), this));
	sizer_3->addWidget(m_min_size_sldr, 1);
	sizer_3->addWidget(m_min_size_text);

	//max size (slider unit = 100 voxels; 101 = no upper limit)
	QHBoxLayout* sizer_4 = make_row(sizerV);
	m_max_size_sldr = new QSlider(Qt::Horizontal, this);
	m_max_size_sldr->setRange(1, 101);
	m_max_size_sldr->setValue(101);
	m_max_size_text = new QLineEdit(QStringLiteral("Ignored"), this);
	m_max_size_text->setFixedWidth(75);
	sizer_4->addWidget(FixedLabel(tr("Max Size:"), this));
	sizer_4->addWidget(m_max_size_sldr, 1);
	sizer_4->addWidget(m_max_size_text);

	//select and calculate
	QHBoxLayout* sizer_5 = make_row(sizerV);
	m_select_both_chk = new QCheckBox(tr("Select Both: "), this);
	m_select_both_chk->setLayoutDirection(Qt::RightToLeft);   // wxALIGN_RIGHT: box after label
	m_colocalization_btn = new QPushButton(tr("Colocalization"), this);
	m_colocalization_btn->setAutoDefault(false);
	sizer_5->addWidget(m_select_both_chk);
	sizer_5->addStretch(1);
	sizer_5->addWidget(m_colocalization_btn);

	sizerV->addStretch(1);

	m_calc_load_a_btn->setAutoDefault(false);
	m_calc_load_b_btn->setAutoDefault(false);

	connect(m_calc_load_a_btn, &QPushButton::clicked, this, &ColocalizationDialog::OnLoadA);
	connect(m_calc_load_b_btn, &QPushButton::clicked, this, &ColocalizationDialog::OnLoadB);
	connect(m_min_size_sldr, &QSlider::valueChanged, this, &ColocalizationDialog::OnMinSizeChange);
	connect(m_min_size_text, &QLineEdit::textEdited, this, &ColocalizationDialog::OnMinSizeText);
	connect(m_max_size_sldr, &QSlider::valueChanged, this, &ColocalizationDialog::OnMaxSizeChange);
	connect(m_max_size_text, &QLineEdit::textEdited, this, &ColocalizationDialog::OnMaxSizeText);
	connect(m_select_both_chk, &QCheckBox::clicked, this, &ColocalizationDialog::OnSelectBothChk);
	connect(m_colocalization_btn, &QPushButton::clicked, this, &ColocalizationDialog::OnColocalizationBtn);
}

void ColocalizationDialog::showEvent(QShowEvent* e)
{
	RefreshNameCompleter();
	GetSettings();
	QDialog::showEvent(e);
}

void ColocalizationDialog::RefreshNameCompleter()
{
	QStringList names;
	if (m_ctx)
	{
		for (VolumeData* vd : m_ctx->VisibleVolumes())
			if (vd) names << vd->GetName().ToQt();
		if (DataManager* mgr = m_ctx->GetDataManager())
			for (int i = 0; i < mgr->GetVolumeNum(); ++i)
				if (VolumeData* vd = mgr->GetVolumeData(i))
				{
					QString n = vd->GetName().ToQt();
					if (!names.contains(n)) names << n;
				}
	}
	m_names->setStringList(names);
}

void ColocalizationDialog::GetSettings()
{
	if (!m_ctx) return;
	VolumeSelector* sel = m_ctx->GetSelector();
	if (!sel) return;
	QSignalBlocker b(m_select_both_chk);
	m_select_both_chk->setChecked(sel->GetSelectBoth());
}

void ColocalizationDialog::SetVolumeA(VolumeData* vd)
{
	if (!vd) return;
	m_vol_a = vd;
	m_calc_a_text->setText(m_vol_a->GetName().ToQt());
}

void ColocalizationDialog::SetVolumeB(VolumeData* vd)
{
	if (!vd) return;
	m_vol_b = vd;
	m_calc_b_text->setText(m_vol_b->GetName().ToQt());
	m_select_both_chk->setChecked(true);
	OnSelectBothChk();
}

void ColocalizationDialog::OnLoadA()
{
	if (!m_ctx) return;
	m_vol_a = m_ctx->CurrentVolume();
	if (m_vol_a)
		m_calc_a_text->setText(m_vol_a->GetName().ToQt());
}

void ColocalizationDialog::OnLoadB()
{
	if (!m_ctx) return;
	m_vol_b = m_ctx->CurrentVolume();
	if (m_vol_b)
		m_calc_b_text->setText(m_vol_b->GetName().ToQt());
	m_select_both_chk->setChecked(true);
	OnSelectBothChk();
}

void ColocalizationDialog::OnMinSizeChange(int v)
{
	m_min_size_text->setText(QString::number(v));
}

void ColocalizationDialog::OnMinSizeText(const QString& s)
{
	bool ok = false;
	int v = s.toInt(&ok);
	if (!ok) return;
	QSignalBlocker b(m_min_size_sldr);
	m_min_size_sldr->setValue(v);   // clamped to the slider range; text keeps the exact value
}

void ColocalizationDialog::OnMaxSizeChange(int v)
{
	m_max_size_text->setText(v == 101 ? QStringLiteral("Ignored") : QString::number(v * 100));
}

void ColocalizationDialog::OnMaxSizeText(const QString& s)
{
	long ival;
	if (s == QLatin1String("Ignored"))
		ival = 10100;
	else
	{
		bool ok = false;
		ival = s.toLong(&ok);
		if (!ok) return;
	}
	QSignalBlocker b(m_max_size_sldr);
	m_max_size_sldr->setValue(int(ival / 100));
}

void ColocalizationDialog::OnSelectBothChk()
{
	if (!m_ctx) return;
	bool select_both = m_select_both_chk->isChecked();
	VolumeSelector* sel = m_ctx->GetSelector();
	VolumeCalculator* calc = m_ctx->GetCalculator();
	if (!sel) return;
	sel->SetSelectBoth(select_both);
	if (select_both && calc)
	{
		// wx VRenderVulkanView::SetVolumeA / SetVolumeB
		calc->SetVolumeA(m_vol_a);
		sel->SetVolume(m_vol_a);
		calc->SetVolumeB(m_vol_b);
	}
}

void ColocalizationDialog::LoadVolumes()
{
	if (!m_ctx) return;
	DataManager* mgr = m_ctx->GetDataManager();
	if (!mgr) return;
	m_vol_a = mgr->GetVolumeData(VVDString(m_calc_a_text->text()));
	m_vol_b = mgr->GetVolumeData(VVDString(m_calc_b_text->text()));
}

void ColocalizationDialog::OnColocalizationBtn()
{
	LoadVolumes();

	if (!m_ctx || !m_vol_a || !m_vol_b)
		return;
	VolumeSelector* sel = m_ctx->GetSelector();
	VolumeCalculator* calc = m_ctx->GetCalculator();
	if (!sel || !calc)
		return;

	bool select_a = true;
	bool select_b = true;
	double min_voxels = m_min_size_text->text().toLong();
	double max_voxels = -1.0;
	{
		QString str = m_max_size_text->text();
		bool ok = false;
		long ival = str.toLong(&ok);
		if (str != QLatin1String("Ignored") && ok)
			max_voxels = ival;
	}
	double thresh = 0.0;

	//save masks
	if (m_vol_a->GetVR()) m_vol_a->GetVR()->return_mask();
	if (m_vol_b->GetVR()) m_vol_b->GetVR()->return_mask();
	if (m_vol_a->GetMask(false)) select_a = false;
	if (m_vol_b->GetMask(false)) select_b = false;

	//volume a
	sel->SetVolume(m_vol_a);
	m_ctx->CompAnalysis(min_voxels, max_voxels, thresh, select_a, true);
	m_vol_a = sel->GetVolume();
	m_ctx->RefreshView();

	//volume b
	sel->SetVolume(m_vol_b);
	m_ctx->CompAnalysis(min_voxels, max_voxels, thresh, select_b, true);
	m_vol_b = sel->GetVolume();
	m_ctx->RefreshView();

	if (m_vol_a) m_vol_a->SetDisp(false);
	if (m_vol_b) m_vol_b->SetDisp(false);

	//volume c = A AND B (calculator op 8)
	calc->SetVolumeA(m_vol_a);
	calc->SetVolumeB(m_vol_b);
	m_ctx->Calculate(8);
	VolumeData* vd = calc->GetResult();
	if (vd)
	{
		sel->SetVolume(vd);
		m_ctx->CompAnalysis(min_voxels, max_voxels, thresh, false, true);
	}

	m_calc_a_text->setText(m_vol_a ? m_vol_a->GetName().ToQt() : QString());
	m_calc_b_text->setText(m_vol_b ? m_vol_b->GetName().ToQt() : QString());

	RefreshNameCompleter();
	m_ctx->RefreshView();
}
