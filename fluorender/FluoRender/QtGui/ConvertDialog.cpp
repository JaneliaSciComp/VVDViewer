#include "ConvertDialog.h"
#include "AnalysisContext.h"

#include <QSlider>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QGroupBox>
#include <QPlainTextEdit>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QDoubleValidator>
#include <QIntValidator>
#include <QSignalBlocker>
#include <QLocale>
#include <algorithm>

#include "DataManager.h"
#include "Converters/VolumeMeshConv.h"
#include "FLIVR/VolumeRenderer.h"
#include "FLIVR/glm.h"
#include "Core/UICallbacks.h"

ConvertDialog::ConvertDialog(AnalysisContext* ctx, QWidget* parent)
	: QDialog(parent),
	m_ctx(ctx),
	m_max_value(1.0),
	m_cnv_vol_mesh_thresh(0.3)
{
	setWindowTitle("Convert");
	setObjectName("ConvertDlg");

	auto* vald_fp2 = new QDoubleValidator(0.0, 1e12, 2, this);
	vald_fp2->setNotation(QDoubleValidator::StandardNotation);
	vald_fp2->setLocale(QLocale::c());
	auto* vald_int = new QIntValidator(0, 2147483647, this);

	QLabel* st = nullptr;

	//group1: convert from volume to mesh
	auto* group1_box = new QGroupBox("Volume to Polygon Mesh", this);
	auto* group1 = new QVBoxLayout(group1_box);

	auto make_row = [&](const char* label, QSlider*& sldr, int val, int lo, int hi,
		QLineEdit*& text, const char* txt, QValidator* vald) {
		auto* h = new QHBoxLayout();
		st = new QLabel(label, group1_box);
		st->setFixedWidth(100);
		sldr = new QSlider(Qt::Horizontal, group1_box);
		sldr->setRange(lo, hi);
		sldr->setValue(val);
		text = new QLineEdit(txt, group1_box);
		text->setFixedWidth(40);
		text->setValidator(vald);
		h->addWidget(st, 0, Qt::AlignVCenter);
		h->addSpacing(10);
		h->addWidget(sldr, 1);
		h->addWidget(text, 0, Qt::AlignVCenter);
		h->addSpacing(15);
		return h;
	};
	//threshold slider and text
	QHBoxLayout* sizer11 = make_row("Threshold:", m_cnv_vol_mesh_thresh_sldr, 30, 1, 99,
		m_cnv_vol_mesh_thresh_text, "30", vald_fp2);
	//downsampling slider and text
	QHBoxLayout* sizer12 = make_row("Downsmp. XY:", m_cnv_vol_mesh_downsample_sldr, 2, 1, 10,
		m_cnv_vol_mesh_downsample_text, "2", vald_int);
	//downsampling in z slider and text
	QHBoxLayout* sizer13 = make_row("Downsmp. Z:", m_cnv_vol_mesh_downsample_z_sldr, 1, 1, 10,
		m_cnv_vol_mesh_downsample_z_text, "1", vald_int);

	//check options
	auto* sizer14 = new QHBoxLayout();
	m_cnv_vol_mesh_usetransf_chk = new QCheckBox("Use transfer function", group1_box);
	m_cnv_vol_mesh_selected_chk = new QCheckBox("Selected Only", group1_box);
	m_cnv_vol_mesh_selected_chk->setChecked(true);
	m_cnv_vol_mesh_weld_chk = new QCheckBox("Weld vertices", group1_box);
	m_cnv_vol_mesh_weld_chk->setChecked(false);
	sizer14->addWidget(m_cnv_vol_mesh_usetransf_chk, 0, Qt::AlignVCenter);
	sizer14->addWidget(m_cnv_vol_mesh_selected_chk, 0, Qt::AlignVCenter);
	sizer14->addWidget(m_cnv_vol_mesh_weld_chk, 0, Qt::AlignVCenter);
	sizer14->addStretch(1);
	m_cnv_vol_mesh_weld_chk->hide();	// hidden in the wx dialog too
	//button
	auto* sizer15 = new QHBoxLayout();
	m_cnv_vol_mesh_convert_btn = new QPushButton("Convert", group1_box);
	sizer15->addStretch(1);
	sizer15->addWidget(m_cnv_vol_mesh_convert_btn, 0, Qt::AlignVCenter);

	group1->addSpacing(5);
	group1->addLayout(sizer11);
	group1->addSpacing(5);
	group1->addLayout(sizer12);
	group1->addSpacing(5);
	group1->addLayout(sizer13);
	group1->addSpacing(5);
	group1->addLayout(sizer14);
	group1->addSpacing(5);
	group1->addLayout(sizer15);
	group1->addSpacing(5);

	//stats text
	auto* sizer2_box = new QGroupBox("Output", this);
	auto* sizer2 = new QVBoxLayout(sizer2_box);
	m_stat_text = new QPlainTextEdit(sizer2_box);
	m_stat_text->setReadOnly(true);
	m_stat_text->setMinimumHeight(100);
	sizer2->addWidget(m_stat_text, 1);

	//all controls
	auto* sizerV = new QVBoxLayout(this);
	sizerV->addSpacing(10);
	sizerV->addWidget(group1_box, 0);
	sizerV->addSpacing(10);
	sizerV->addWidget(sizer2_box, 1);
	sizerV->addSpacing(10);

	connect(m_cnv_vol_mesh_thresh_sldr, &QSlider::valueChanged, this, &ConvertDialog::OnCnvVolMeshThreshChange);
	connect(m_cnv_vol_mesh_thresh_text, &QLineEdit::textChanged, this, &ConvertDialog::OnCnvVolMeshThreshText);
	connect(m_cnv_vol_mesh_downsample_sldr, &QSlider::valueChanged, this, &ConvertDialog::OnCnvVolMeshDownsampleChange);
	connect(m_cnv_vol_mesh_downsample_text, &QLineEdit::textChanged, this, &ConvertDialog::OnCnvVolMeshDownsampleText);
	connect(m_cnv_vol_mesh_downsample_z_sldr, &QSlider::valueChanged, this, &ConvertDialog::OnCnvVolMeshDownsampleZChange);
	connect(m_cnv_vol_mesh_downsample_z_text, &QLineEdit::textChanged, this, &ConvertDialog::OnCnvVolMeshDownsampleZText);
	connect(m_cnv_vol_mesh_convert_btn, &QPushButton::clicked, this, &ConvertDialog::OnCnvVolMeshConvert);

	resize(400, 300);
}

ConvertDialog::~ConvertDialog() = default;

void ConvertDialog::showEvent(QShowEvent* e)
{
	QDialog::showEvent(e);
	GetSettings();
}

void ConvertDialog::GetSettings()
{
	if (!m_ctx)
		return;
	VolumeData* sel_vol = m_ctx->CurrentVolume();
	if (!sel_vol)
		return;

	m_max_value = sel_vol->GetMaxValue();
	if (m_max_value <= 0.0)
		m_max_value = 255.0;
	QSignalBlocker b1(m_cnv_vol_mesh_thresh_sldr);
	QSignalBlocker b2(m_cnv_vol_mesh_thresh_text);
	m_cnv_vol_mesh_thresh_sldr->setRange(0, int(m_max_value));
	m_cnv_vol_mesh_thresh_sldr->setValue(int(m_cnv_vol_mesh_thresh * m_max_value + 0.5));
	m_cnv_vol_mesh_thresh_text->setText(QString::asprintf("%d", int(m_cnv_vol_mesh_thresh * m_max_value + 0.5)));
}

//threshold
void ConvertDialog::OnCnvVolMeshThreshChange(int ival)
{
	m_cnv_vol_mesh_thresh_text->setText(QString::asprintf("%d", ival));
}

void ConvertDialog::OnCnvVolMeshThreshText(const QString& str)
{
	bool ok = false;
	double val = QLocale::c().toDouble(str, &ok);
	if (!ok)
		val = 0.0;
	m_cnv_vol_mesh_thresh = val / m_max_value;
	QSignalBlocker b(m_cnv_vol_mesh_thresh_sldr);
	m_cnv_vol_mesh_thresh_sldr->setValue(int(val + 0.5));
}

//downsampling
void ConvertDialog::OnCnvVolMeshDownsampleChange(int ival)
{
	m_cnv_vol_mesh_downsample_text->setText(QString::asprintf("%d", ival));
}

void ConvertDialog::OnCnvVolMeshDownsampleText(const QString& str)
{
	QSignalBlocker b(m_cnv_vol_mesh_downsample_sldr);
	m_cnv_vol_mesh_downsample_sldr->setValue(str.toInt());
}

//downsampling Z
void ConvertDialog::OnCnvVolMeshDownsampleZChange(int ival)
{
	m_cnv_vol_mesh_downsample_z_text->setText(QString::asprintf("%d", ival));
}

void ConvertDialog::OnCnvVolMeshDownsampleZText(const QString& str)
{
	QSignalBlocker b(m_cnv_vol_mesh_downsample_z_sldr);
	m_cnv_vol_mesh_downsample_z_sldr->setValue(str.toInt());
}

void ConvertDialog::OnCnvVolMeshConvert()
{
	if (!m_ctx)
		return;
	VolumeData* sel_vol = m_ctx->CurrentVolume();
	if (!sel_vol || !sel_vol->GetTexture())
		return;

	std::unique_ptr<vvd::Progress> prog_diag = vvd::UI::CreateProgress(
		L"VVDViewer: Convert volume to polygon data",
		L"Converting... Please wait.",
		100,
		vvd::PROGRESS_APP_MODAL | vvd::PROGRESS_SMOOTH | vvd::PROGRESS_ELAPSED | vvd::PROGRESS_AUTO_HIDE);
	if (prog_diag)
		prog_diag->Update(50);

	VolumeMeshConv converter;
	converter.SetVolume(sel_vol->GetTexture()->get_nrrd(0));
	double spcx = 1.0, spcy = 1.0, spcz = 1.0;
	sel_vol->GetSpacings(spcx, spcy, spcz);
	converter.SetVolumeSpacings(spcx, spcy, spcz);
	converter.SetMaxValue(sel_vol->GetMaxValue());
	//iso value
	converter.SetIsoValue(m_cnv_vol_mesh_thresh);
	//downsampling
	converter.SetDownsample(m_cnv_vol_mesh_downsample_text->text().toInt());
	//downsampling Z
	converter.SetDownsampleZ(m_cnv_vol_mesh_downsample_z_text->text().toInt());
	//use transfer function
	if (m_cnv_vol_mesh_usetransf_chk->isChecked())
	{
		double gamma = sel_vol->Get3DGamma();
		double lo_thresh = sel_vol->GetLeftThresh();
		double hi_thresh = sel_vol->GetRightThresh();
		double offset = sel_vol->GetOffset();
		double gm_thresh = sel_vol->GetBoundary();
		converter.SetVolumeTransfer(gamma, lo_thresh, hi_thresh, offset, gm_thresh);
		converter.SetVolumeUseTrans(true);
	}
	else
		converter.SetVolumeUseTrans(false);
	//use selection
	if (m_cnv_vol_mesh_selected_chk->isChecked() && sel_vol->GetVR() &&
		sel_vol->GetTexture()->nmask() >= 0)
	{
		sel_vol->GetVR()->return_mask();
		converter.SetVolumeMask(sel_vol->GetTexture()->get_nrrd(sel_vol->GetTexture()->nmask()));
		converter.SetVolumeUseMask(true);
	}
	else
		converter.SetVolumeUseMask(false);
	//start converting
	converter.Convert();

	if (!converter.isModelValid())
	{
		m_stat_text->appendPlainText("The generated mesh is empty.");
		return;
	}

	GLMmodel* mesh = converter.GetMesh();

	if (prog_diag)
		prog_diag->Update(90);

	if (mesh)
	{
		if (m_cnv_vol_mesh_weld_chk->isChecked())
			glmWeld(mesh, float(std::min(spcx, std::min(spcy, spcz) * 0.001)));
		float area = 0.0f;
		float scale[3] = { 1.0f, 1.0f, 1.0f };
		glmArea(mesh, scale, &area);
		DataManager* mgr = m_ctx->GetDataManager();
		MeshData* md = nullptr;
		if (mgr && mgr->LoadMeshData(mesh))
			md = mgr->GetLastMeshData();
		if (md)
		{
			Q_EMIT meshCreated(md);
			m_stat_text->appendPlainText(
				QString("The surface area of mesh object ") +
				md->GetName().ToQt() + " is " +
				QString::asprintf("%f", area));
		}
	}
}
