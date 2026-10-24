#include "NoiseReductionDialog.h"
#include "BrushToolDialog.h"
#include "AnalysisContext.h"

#include <QSlider>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QDoubleValidator>
#include <QIntValidator>
#include <QSignalBlocker>
#include <QLocale>

#include "DataManager.h"
#include "VolumeSelector.h"
#include "VolumeCalculator.h"

namespace
{
	// make selector / calculator operate on the current volume, as the wx tree
	// selection does (VRenderVulkanView::SetVolumeA)
	VolumeData* SyncSelection(AnalysisContext* ctx)
	{
		if (!ctx)
			return nullptr;
		VolumeData* vd = ctx->CurrentVolume();
		if (!vd)
			return nullptr;
		if (VolumeSelector* sel = ctx->GetSelector())
			if (sel->GetVolume() != vd)
				sel->SetVolume(vd);
		if (VolumeCalculator* cal = ctx->GetCalculator())
			if (cal->GetVolumeA() != vd)
				cal->SetVolumeA(vd);
		return vd;
	}
}

NoiseReductionDialog::NoiseReductionDialog(AnalysisContext* ctx, QWidget* parent)
	: QDialog(parent),
	m_ctx(ctx),
	m_max_value(255.0),
	m_dft_thresh(0.0),
	m_dft_size(0.0),
	m_previewed(false)
{
	setWindowTitle("Noise Reduction");
	setObjectName("NoiseCancellingDlg");

	auto* vald_fp1 = new QDoubleValidator(0.0, 1e12, 1, this);
	vald_fp1->setNotation(QDoubleValidator::StandardNotation);
	vald_fp1->setLocale(QLocale::c());
	auto* vald_int = new QIntValidator(0, 2147483647, this);

	//group1
	auto* sizer1 = new QHBoxLayout();
	auto* st = new QLabel("Threshold:", this);
	st->setFixedWidth(75);
	m_threshold_sldr = new QSlider(Qt::Horizontal, this);
	m_threshold_sldr->setRange(0, 2550);
	m_threshold_sldr->setValue(0);
	m_threshold_text = new QLineEdit("0.0", this);
	m_threshold_text->setFixedWidth(40);
	m_threshold_text->setValidator(vald_fp1);
	m_preview_btn = new QPushButton("Preview", this);
	m_preview_btn->setFixedWidth(70);
	sizer1->addWidget(st, 0, Qt::AlignVCenter);
	sizer1->addWidget(m_threshold_sldr, 1);
	sizer1->addWidget(m_threshold_text, 0, Qt::AlignVCenter);
	sizer1->addSpacing(5);
	sizer1->addWidget(m_preview_btn, 0, Qt::AlignVCenter);

	//group2
	auto* sizer2 = new QHBoxLayout();
	st = new QLabel("Voxel Size:", this);
	st->setFixedWidth(75);
	m_voxel_sldr = new QSlider(Qt::Horizontal, this);
	m_voxel_sldr->setRange(1, 500);
	m_voxel_sldr->setValue(1);
	m_voxel_text = new QLineEdit("1", this);
	m_voxel_text->setFixedWidth(40);
	m_voxel_text->setValidator(vald_int);
	m_erase_btn = new QPushButton("Erase", this);
	m_erase_btn->setFixedWidth(70);
	sizer2->addWidget(st, 0, Qt::AlignVCenter);
	sizer2->addWidget(m_voxel_sldr, 1);
	sizer2->addWidget(m_voxel_text, 0, Qt::AlignVCenter);
	sizer2->addSpacing(5);
	sizer2->addWidget(m_erase_btn, 0, Qt::AlignVCenter);

	//group3
	auto* sizer3 = new QHBoxLayout();
	m_enhance_sel_chk = new QCheckBox("Enhance selection", this);
	m_enhance_sel_chk->setLayoutDirection(Qt::RightToLeft); // wxALIGN_RIGHT: box after label
	sizer3->addWidget(m_enhance_sel_chk, 0, Qt::AlignVCenter);
	sizer3->addStretch(1);

	//group4
	auto* sizer4 = new QHBoxLayout();
	st = new QLabel(
		"Check this option if selections are too dim. It is equivalent to\n"
		"adjusting the Equalization values in the Output Adjustment\n panel.", this);
	sizer4->addWidget(st, 0, Qt::AlignVCenter);
	sizer4->addStretch(1);

	//all controls
	auto* sizerV = new QVBoxLayout(this);
	sizerV->addSpacing(10);
	sizerV->addLayout(sizer1);
	sizerV->addSpacing(10);
	sizerV->addLayout(sizer2);
	sizerV->addSpacing(10);
	sizerV->addLayout(sizer3);
	sizerV->addLayout(sizer4);
	sizerV->addStretch(1);

	connect(m_threshold_sldr, &QSlider::valueChanged, this, &NoiseReductionDialog::OnThresholdChange);
	connect(m_threshold_text, &QLineEdit::textChanged, this, &NoiseReductionDialog::OnThresholdText);
	connect(m_voxel_sldr, &QSlider::valueChanged, this, &NoiseReductionDialog::OnVoxelChange);
	connect(m_voxel_text, &QLineEdit::textChanged, this, &NoiseReductionDialog::OnVoxelText);
	connect(m_preview_btn, &QPushButton::clicked, this, &NoiseReductionDialog::OnPreviewBtn);
	connect(m_erase_btn, &QPushButton::clicked, this, &NoiseReductionDialog::OnEraseBtn);
	connect(m_enhance_sel_chk, &QCheckBox::toggled, this, &NoiseReductionDialog::OnEnhanceSelChk);

	resize(400, 150);
}

NoiseReductionDialog::~NoiseReductionDialog() = default;

void NoiseReductionDialog::showEvent(QShowEvent* e)
{
	QDialog::showEvent(e);
	GetSettings();
}

void NoiseReductionDialog::GetSettings()
{
	if (!m_ctx)
		return;
	{
		// reflect the view flag (wx leaves the box unchecked although the
		// view defaults to enhance = true)
		QSignalBlocker b(m_enhance_sel_chk);
		m_enhance_sel_chk->setChecked(m_ctx->GetEnhanceSelection());
	}

	// wx: the defaults live in the brush dialog (nr_thresh / nr_size)
	if (BrushToolDialog* bd = m_ctx->GetBrushDialog())
	{
		m_dft_thresh = bd->GetDftNRThresh();
		m_dft_size = bd->GetDftNRSize();
	}

	VolumeData* sel_vol = m_ctx->CurrentVolume();
	if (!sel_vol)
		return;

	m_max_value = sel_vol->GetMaxValue();
	if (m_max_value <= 0.0)
		m_max_value = 255.0;
	//threshold (ChangeValue in wx: no text event)
	{
		QSignalBlocker b1(m_threshold_sldr);
		QSignalBlocker b2(m_threshold_text);
		m_threshold_sldr->setRange(0, int(m_max_value * 10.0));
		m_threshold_sldr->setValue(int(m_dft_thresh * m_max_value * 10.0 + 0.5));
		m_threshold_text->setText(QString::asprintf("%.1f", m_dft_thresh * m_max_value));
	}
	//voxel
	{
		int nx = 1, ny = 1, nz = 1;
		sel_vol->GetResolution(nx, ny, nz);
		QSignalBlocker b1(m_voxel_sldr);
		QSignalBlocker b2(m_voxel_text);
		m_voxel_sldr->setRange(1, nx > 1 ? nx : 1);
		m_voxel_sldr->setValue(int(m_dft_size));
		m_voxel_text->setText(QString::asprintf("%d", int(m_dft_size)));
	}
	m_previewed = false;
}

//threshold
void NoiseReductionDialog::OnThresholdChange(int ival)
{
	m_threshold_text->setText(QString::asprintf("%.1f", double(ival) / 10.0));
}

void NoiseReductionDialog::OnThresholdText(const QString& str)
{
	bool ok = false;
	double val = QLocale::c().toDouble(str, &ok);
	if (!ok)
		val = 0.0;
	m_dft_thresh = val / m_max_value;
	{
		QSignalBlocker b(m_threshold_sldr);
		m_threshold_sldr->setValue(int(val * 10.0 + 0.5));
	}

	//change mask threshold
	if (!m_ctx)
		return;
	if (VolumeData* sel_vol = m_ctx->CurrentVolume())
		sel_vol->SetMaskThreshold(m_dft_thresh);
	m_ctx->RefreshView();
}

//voxel size
void NoiseReductionDialog::OnVoxelChange(int ival)
{
	m_voxel_text->setText(QString::asprintf("%d", ival));
}

void NoiseReductionDialog::OnVoxelText(const QString& str)
{
	bool ok = false;
	long ival = str.toLong(&ok);
	if (!ok)
		ival = 0;
	m_dft_size = double(ival);
	QSignalBlocker b(m_voxel_sldr);
	m_voxel_sldr->setValue(int(ival));
}

void NoiseReductionDialog::OnPreviewBtn()
{
	if (!m_ctx)
		return;
	VolumeData* sel_vol = SyncSelection(m_ctx);
	if (!sel_vol)
		return;

	m_ctx->CompAnalysis(0.0, m_dft_size, m_dft_thresh, false, false, int(m_dft_size) + 1);
	//change mask threshold
	sel_vol->SetUseMaskThreshold(true);
	if (BrushToolDialog* bd = m_ctx->GetBrushDialog())
	{
		bd->SetDftNRThresh(m_dft_thresh);
		bd->SetDftNRSize(m_dft_size);
	}
	m_ctx->RefreshView();
	m_previewed = true;
}

void NoiseReductionDialog::OnEraseBtn()
{
	// wx: frame->GetTree()->BrushErase() -> DataTreeCtrl::BrushCreateInv():
	// SetVolumeA(selected volume); Calculate(6, <its group>), or for a volume
	// in NA mode Calculate(11) with the group's NA label volumes as A / B / C
	if (m_ctx && SyncSelection(m_ctx))
		m_ctx->BrushErase();
	m_previewed = false;
}

void NoiseReductionDialog::OnEnhanceSelChk(bool checked)
{
	if (!m_ctx)
		return;
	m_ctx->SetEnhanceSelection(checked);
}
