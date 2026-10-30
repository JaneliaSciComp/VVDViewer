#include "ColorDepthMIPDialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

ColorDepthMIPDialog::ColorDepthMIPDialog(QWidget* parent) : QDialog(parent)
{
	setWindowTitle(tr("Save Color Depth MIP"));
	setObjectName("cdm_dialog");
	auto* top = new QVBoxLayout(this);
	m_info = new QLabel(this);
	m_info->setWordWrap(true);
	top->addWidget(m_info);

	// macro: Dialog.addRadioButtonGroup("Automatic Brightness adjustment", itembr, 3, 1, ...)
	auto* gb = new QGroupBox(tr("Automatic Brightness adjustment"), this);
	auto* gl = new QVBoxLayout(gb);
	auto* grp = new QButtonGroup(this);
	for (int i = 0; i < 5; i++)
	{
		m_mode[i] = new QRadioButton(QString::fromUtf8(cdm::ModeLabel(i)), gb);
		m_mode[i]->setObjectName(QString("cdm_mode%1").arg(i));
		grp->addButton(m_mode[i], i);
		gl->addWidget(m_mode[i]);
		connect(m_mode[i], &QRadioButton::toggled, this, [this]() { UpdateEnabled(); });
	}
	top->addWidget(gb);

	m_scale = new QCheckBox(tr("Add color scale"), this);
	m_scale->setObjectName("cdm_scale");
	m_reverse = new QCheckBox(tr("Reversed color"), this);
	m_reverse->setObjectName("cdm_reverse");
	m_gamma = new QCheckBox(tr("Apply Gamma 1.4"), this);
	m_gamma->setObjectName("cdm_gamma");
	m_skeleton = new QCheckBox(tr("Skeleton bolded MIP"), this);
	m_skeleton->setObjectName("cdm_skeleton");
	top->addWidget(m_scale);
	top->addWidget(m_reverse);
	top->addWidget(m_gamma);
	top->addWidget(m_skeleton);

	auto* dg = new QGroupBox(tr("DSLT version"), this);
	auto* dl = new QHBoxLayout(dg);
	m_dslt_normal = new QRadioButton(tr("Normal"), dg);
	m_dslt_normal->setObjectName("cdm_dslt_normal");
	m_dslt_line = new QRadioButton(tr("Line"), dg);
	m_dslt_line->setObjectName("cdm_dslt_line");
	auto* dgrp = new QButtonGroup(this);
	dgrp->addButton(m_dslt_normal);
	dgrp->addButton(m_dslt_line);
	dl->addWidget(m_dslt_normal);
	dl->addWidget(m_dslt_line);
	dl->addStretch(1);
	top->addWidget(dg);

	auto* form = new QFormLayout();
	m_start = new QSpinBox(this);
	m_start->setObjectName("cdm_start");
	m_start->setRange(0, 100000);
	m_end = new QSpinBox(this);
	m_end->setObjectName("cdm_end");
	m_end->setRange(0, 100000);
	form->addRow(tr("Starting MIP slice"), m_start);
	form->addRow(tr("Ending MIP slice, larger number will be the last slice"), m_end);
	// macro: second dialog, segmentation modes only
	m_mean = new QSpinBox(this);
	m_mean->setObjectName("cdm_mean");
	m_mean->setRange(1, 255);
	m_mean_label = new QLabel(tr("Desired average value for the Auto-Brightness adjustment /255  150-220"), this);
	m_mean_label->setWordWrap(true);
	form->addRow(m_mean_label, m_mean);
	top->addLayout(form);

	auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
	top->addWidget(bb);

	SetOptions(cdm::Options());
}

void ColorDepthMIPDialog::SetVolumeInfo(const QString& text)
{
	m_info->setText(text);
	m_info->setVisible(!text.isEmpty());
}

void ColorDepthMIPDialog::SetOptions(const cdm::Options& o)
{
	int mode = (o.mode >= 0 && o.mode < 5) ? o.mode : 0;
	m_mode[mode]->setChecked(true);
	m_scale->setChecked(o.colorScale);
	m_reverse->setChecked(o.reversed);
	m_gamma->setChecked(o.gamma14);
	m_skeleton->setChecked(o.skeletonBolded);
	(o.dsltLine ? m_dslt_line : m_dslt_normal)->setChecked(true);
	m_start->setValue(o.startMIP);
	m_end->setValue(o.endMIP);
	m_mean->setValue(o.desiredMean);
	m_threads = o.threads;
	UpdateEnabled();
}

cdm::Options ColorDepthMIPDialog::GetOptions() const
{
	cdm::Options o;
	for (int i = 0; i < 5; i++)
		if (m_mode[i]->isChecked()) o.mode = i;
	o.colorScale = m_scale->isChecked();
	o.reversed = m_reverse->isChecked();
	o.gamma14 = m_gamma->isChecked();
	o.skeletonBolded = m_skeleton->isChecked();
	o.dsltLine = m_dslt_line->isChecked();
	o.startMIP = m_start->value();
	o.endMIP = m_end->value();
	o.desiredMean = m_mean->value();
	o.threads = m_threads;
	return o;
}

void ColorDepthMIPDialog::UpdateEnabled()
{
	// the macro asks for the desired mean only in the segmentation modes
	const bool seg = m_mode[cdm::Options::SegmentationAuto]->isChecked() ||
		m_mode[cdm::Options::SegmentationNoLowerCut]->isChecked();
	m_mean->setEnabled(seg);
	m_mean_label->setEnabled(seg);
}
