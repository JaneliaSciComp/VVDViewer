#include "CountingDialog.h"
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
#include <QSettings>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>

#include "DataManager.h"
#include "VolumeSelector.h"
#include "VolumeCalculator.h"

namespace
{
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

	// same lookup as the wx CountingDlg / BrushToolDlg::LoadDefault
	QString DefaultBrushSettingsPath()
	{
		const QString expath = QCoreApplication::applicationDirPath();
#ifdef __APPLE__
		return expath + "/../Resources/default_brush_settings.dft";
#else
		QString dft = expath + "/default_brush_settings.dft";
		if (!QFileInfo::exists(dft))
		{
#ifdef _WIN32
			QString cfg = qEnvironmentVariable("APPDATA");
#else
			QString cfg = QDir::homePath();
#endif
			dft = cfg + "/default_brush_settings.dft";
		}
		return dft;
#endif
	}
}

CountingDialog::CountingDialog(AnalysisContext* ctx, QWidget* parent)
	: QDialog(parent),
	m_ctx(ctx),
	m_max_value(255.0),
	m_dft_thresh(0.0)
{
	setWindowTitle("Counting and Volume");
	setObjectName("CountingDlg");

	auto* vald_fp1 = new QDoubleValidator(0.0, 1e12, 1, this);
	vald_fp1->setNotation(QDoubleValidator::StandardNotation);
	vald_fp1->setLocale(QLocale::c());
	auto* vald_int = new QIntValidator(0, 2147483647, this);

	QLabel* st = nullptr;

	//threshold of ccl
	auto* sizer_1 = new QHBoxLayout();
	st = new QLabel("Threshold:", this);
	st->setFixedWidth(75);
	m_ca_thresh_sldr = new QSlider(Qt::Horizontal, this);
	m_ca_thresh_sldr->setRange(0, 2550);
	m_ca_thresh_sldr->setValue(0);
	m_ca_thresh_text = new QLineEdit("0.0", this);
	m_ca_thresh_text->setFixedWidth(50);
	m_ca_thresh_text->setValidator(vald_fp1);
	m_ca_analyze_btn = new QPushButton("Analyze", this);
	sizer_1->addWidget(st, 0, Qt::AlignVCenter);
	sizer_1->addWidget(m_ca_thresh_sldr, 1);
	sizer_1->addWidget(m_ca_thresh_text, 0, Qt::AlignVCenter);
	sizer_1->addWidget(m_ca_analyze_btn, 0, Qt::AlignVCenter);

	//size of ccl
	auto* sizer_2 = new QHBoxLayout();
	m_ca_select_only_chk = new QCheckBox("Selct. Only", this);
	m_ca_select_only_chk->setMinimumWidth(95);
	sizer_2->addWidget(m_ca_select_only_chk, 0, Qt::AlignVCenter);
	sizer_2->addStretch(1);
	st = new QLabel("Min:", this);
	st->setFixedWidth(35);
	sizer_2->addWidget(st, 0, Qt::AlignVCenter);
	m_ca_min_text = new QLineEdit("0", this);
	m_ca_min_text->setFixedWidth(40);
	m_ca_min_text->setValidator(vald_int);
	sizer_2->addWidget(m_ca_min_text, 0, Qt::AlignVCenter);
	st = new QLabel("vx", this);
	st->setFixedWidth(15);
	sizer_2->addWidget(st, 0, Qt::AlignVCenter);
	sizer_2->addStretch(1);
	st = new QLabel("Max:", this);
	st->setFixedWidth(35);
	sizer_2->addWidget(st, 0, Qt::AlignVCenter);
	m_ca_max_text = new QLineEdit("1000", this);
	m_ca_max_text->setFixedWidth(40);
	m_ca_max_text->setValidator(vald_int);
	sizer_2->addWidget(m_ca_max_text, 0, Qt::AlignVCenter);
	st = new QLabel("vx", this);
	st->setFixedWidth(15);
	sizer_2->addWidget(st, 0, Qt::AlignVCenter);
	sizer_2->addStretch(1);
	m_ca_ignore_max_chk = new QCheckBox("Ignore Max", this);
	sizer_2->addWidget(m_ca_ignore_max_chk, 0, Qt::AlignVCenter);

	//text result
	auto* sizer_3 = new QHBoxLayout();
	st = new QLabel("Components:  ", this);
	sizer_3->addWidget(st, 0, Qt::AlignVCenter);
	m_ca_comps_text = new QLineEdit("0", this);
	m_ca_comps_text->setMinimumWidth(60);
	m_ca_comps_text->setReadOnly(true);
	sizer_3->addWidget(m_ca_comps_text, 2, Qt::AlignVCenter);
	st = new QLabel("Total Volume:  ", this);
	sizer_3->addWidget(st, 0, Qt::AlignVCenter);
	m_ca_volume_text = new QLineEdit("0", this);
	m_ca_volume_text->setMinimumWidth(60);
	m_ca_volume_text->setReadOnly(true);
	sizer_3->addWidget(m_ca_volume_text, 2, Qt::AlignVCenter);
	st = new QLabel("vx", this);
	st->setFixedWidth(15);
	sizer_3->addWidget(st, 0, Qt::AlignVCenter);
	sizer_3->addSpacing(5);
	m_ca_vol_unit_text = new QLineEdit("0", this);
	m_ca_vol_unit_text->setMinimumWidth(90);
	m_ca_vol_unit_text->setReadOnly(true);
	sizer_3->addWidget(m_ca_vol_unit_text, 3, Qt::AlignVCenter);

	//export
	auto* sizer_4 = new QHBoxLayout();
	sizer_4->addStretch(1);
	st = new QLabel("Export:", this);
	sizer_4->addWidget(st, 0, Qt::AlignVCenter);
	m_ca_multi_chann_btn = new QPushButton("Multi-Channels", this);
	m_ca_random_color_btn = new QPushButton("Random Colors", this);
	m_ca_annotations_btn = new QPushButton("Show Annotations", this);
	sizer_4->addWidget(m_ca_multi_chann_btn, 0, Qt::AlignVCenter);
	sizer_4->addWidget(m_ca_random_color_btn, 0, Qt::AlignVCenter);
	sizer_4->addWidget(m_ca_annotations_btn, 0, Qt::AlignVCenter);

	//all controls
	auto* sizerV = new QVBoxLayout(this);
	sizerV->addSpacing(10);
	sizerV->addLayout(sizer_1);
	sizerV->addSpacing(10);
	sizerV->addLayout(sizer_2);
	sizerV->addSpacing(10);
	sizerV->addLayout(sizer_3);
	sizerV->addSpacing(10);
	sizerV->addLayout(sizer_4);
	sizerV->addStretch(1);

	LoadDefault();

	connect(m_ca_thresh_sldr, &QSlider::valueChanged, this, &CountingDialog::OnCAThreshChange);
	connect(m_ca_thresh_text, &QLineEdit::textChanged, this, &CountingDialog::OnCAThreshText);
	connect(m_ca_ignore_max_chk, &QCheckBox::toggled, this, &CountingDialog::OnCAIgnoreMaxChk);
	connect(m_ca_analyze_btn, &QPushButton::clicked, this, &CountingDialog::OnCAAnalyzeBtn);
	connect(m_ca_multi_chann_btn, &QPushButton::clicked, this, &CountingDialog::OnCAMultiChannBtn);
	connect(m_ca_random_color_btn, &QPushButton::clicked, this, &CountingDialog::OnCARandomColorBtn);
	connect(m_ca_annotations_btn, &QPushButton::clicked, this, &CountingDialog::OnCAAnnotationsBtn);

	resize(400, 150);
}

CountingDialog::~CountingDialog() = default;

void CountingDialog::LoadDefault()
{
	const QString dft = DefaultBrushSettingsPath();
	if (!QFileInfo::exists(dft))
		return;
	// wxFileConfig files are plain key=value lines; QSettings' INI reader
	// exposes top-level keys directly
	QSettings fconfig(dft, QSettings::IniFormat);

	//selected only
	if (fconfig.contains("ca_select_only"))
		m_ca_select_only_chk->setChecked(fconfig.value("ca_select_only").toBool());
	//min voxel
	if (fconfig.contains("ca_min"))
		m_ca_min_text->setText(QString::asprintf("%d", fconfig.value("ca_min").toInt()));
	//max voxel
	if (fconfig.contains("ca_max"))
		m_ca_max_text->setText(QString::asprintf("%d", fconfig.value("ca_max").toInt()));
	//ignore max
	if (fconfig.contains("ca_ignore_max"))
	{
		bool bval = fconfig.value("ca_ignore_max").toBool();
		m_ca_ignore_max_chk->setChecked(bval);
		m_ca_max_text->setEnabled(!bval);
	}
	//thresh
	if (fconfig.contains("ca_thresh"))
	{
		bool ok = false;
		double v = QLocale::c().toDouble(fconfig.value("ca_thresh").toString(), &ok);
		if (ok)
		{
			m_dft_thresh = v;
			m_ca_thresh_sldr->setRange(0, int(m_max_value * 10.0));
			m_ca_thresh_sldr->setValue(int(m_dft_thresh * m_max_value * 10.0 + 0.5));
			m_ca_thresh_text->setText(QString::asprintf("%.1f", m_dft_thresh * m_max_value));
		}
	}
}

void CountingDialog::showEvent(QShowEvent* e)
{
	QDialog::showEvent(e);
	GetSettings();
}

void CountingDialog::GetSettings()
{
	if (!m_ctx)
		return;
	VolumeData* sel_vol = m_ctx->CurrentVolume();
	if (!sel_vol)
		return;

	m_max_value = sel_vol->GetMaxValue();
	if (m_max_value <= 0.0)
		m_max_value = 255.0;
	QSignalBlocker b1(m_ca_thresh_sldr);
	QSignalBlocker b2(m_ca_thresh_text);
	m_ca_thresh_sldr->setRange(0, int(m_max_value * 10.0));
	m_ca_thresh_sldr->setValue(int(m_dft_thresh * m_max_value * 10.0 + 0.5));
	m_ca_thresh_text->setText(QString::asprintf("%.1f", m_dft_thresh * m_max_value));
}

void CountingDialog::OnCAThreshChange(int ival)
{
	m_ca_thresh_text->setText(QString::asprintf("%.1f", double(ival) / 10.0));
}

void CountingDialog::OnCAThreshText(const QString& str)
{
	bool ok = false;
	double val = QLocale::c().toDouble(str, &ok);
	if (!ok)
		val = 0.0;
	m_dft_thresh = val / m_max_value;
	{
		QSignalBlocker b(m_ca_thresh_sldr);
		m_ca_thresh_sldr->setValue(int(val * 10.0 + 0.5));
	}

	//change mask threshold
	if (!m_ctx)
		return;
	if (VolumeData* sel_vol = m_ctx->CurrentVolume())
		sel_vol->SetMaskThreshold(m_dft_thresh);
	m_ctx->RefreshView();
}

void CountingDialog::OnCAAnalyzeBtn()
{
	if (!m_ctx)
		return;
	VolumeData* sel_vol = SyncSelection(m_ctx);
	if (!sel_vol)
		return;

	bool select = m_ca_select_only_chk->isChecked();
	double min_voxels = QLocale::c().toDouble(m_ca_min_text->text());
	double max_voxels = QLocale::c().toDouble(m_ca_max_text->text());
	bool ignore_max = m_ca_ignore_max_chk->isChecked();

	int comps = m_ctx->CompAnalysis(min_voxels, ignore_max ? -1.0 : max_voxels,
		m_dft_thresh, select, true);
	VolumeSelector* selector = m_ctx->GetSelector();
	int volume = selector ? selector->GetVolumeNum() : 0;
	//change mask threshold
	sel_vol->SetUseMaskThreshold(true);
	m_ca_comps_text->setText(QString::number(comps));
	m_ca_volume_text->setText(QString::number(volume));

	double spcx = 1.0, spcy = 1.0, spcz = 1.0;
	sel_vol->GetSpacings(spcx, spcy, spcz);
	double vol_unit = volume * spcx * spcy * spcz;
	QString vol_unit_text = QString::asprintf("%.0f", vol_unit) + " ";
	switch (m_ctx->GetScaleBarUnit())
	{
	case 0:
		vol_unit_text += QString::fromWCharArray(L"nm³");
		break;
	case 1:
	default:
		vol_unit_text += QString::fromWCharArray(L"μm³");
		break;
	case 2:
		vol_unit_text += QString::fromWCharArray(L"mm³");
		break;
	}
	m_ca_vol_unit_text->setText(vol_unit_text);

	m_ctx->RefreshView();
}

void CountingDialog::OnCAIgnoreMaxChk(bool checked)
{
	m_ca_max_text->setEnabled(!checked);
}

void CountingDialog::OnCAMultiChannBtn()
{
	if (m_ctx && SyncSelection(m_ctx))
		m_ctx->CompExport(0, m_ca_select_only_chk->isChecked());
}

void CountingDialog::OnCARandomColorBtn()
{
	if (m_ctx && SyncSelection(m_ctx))
		m_ctx->CompExport(1, m_ca_select_only_chk->isChecked());
}

void CountingDialog::OnCAAnnotationsBtn()
{
	if (m_ctx)
		m_ctx->ShowAnnotations();
}
