#include "CaptureDialog.h"
#include <QTimer>

#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QIntValidator>
#include <QMessageBox>
#include <QFileInfo>
#include <QDir>
#include <map>

#include "RenderCanvas.h"
#include "DataManager.h"

namespace
{
	bool s_cap_group_each = false;   // wx VRenderView::m_cap_group_each
}

bool CaptureDialog::GetGroupEach() { return s_cap_group_each; }
void CaptureDialog::SetGroupEach(bool b) { s_cap_group_each = b; }

bool CaptureDialog::Run(QWidget* parent, RenderCanvas* canvas, const Hooks& hooks)
{
	if (!canvas) return false;

	Options opt;
	// wx: VRenderFrame::SetSaveProject(GetSettingDlg()->GetProjSave())
	opt.proj_save = hooks.get_proj_save ? hooks.get_proj_save() : false;

	const QSize disp = canvas->GetDeviceSize();
	int cap_resx = canvas->GetCaptureResX();
	if (cap_resx <= 0) cap_resx = disp.width();
	int cap_resy = canvas->GetCaptureResY();
	if (cap_resy <= 0) cap_resy = disp.height();

	int cap_orgresx = -1, cap_orgresy = -1;
	canvas->Get1x1DispSize(cap_orgresx, cap_orgresy);
	if (cap_orgresx <= 0) cap_orgresx = disp.width();
	if (cap_orgresy <= 0) cap_orgresy = disp.height();

	const int cap_dispresx = disp.width();
	const int cap_dispresy = disp.height();

	// wxFileDialog(m_frame, "Save captured image", "", "", "*.tif", wxFD_SAVE|wxFD_OVERWRITE_PROMPT)
	QFileDialog dlg(parent, QObject::tr("Save captured image"), QString(), QStringLiteral("*.tif"));
	dlg.setAcceptMode(QFileDialog::AcceptSave);
	dlg.setFileMode(QFileDialog::AnyFile);
	dlg.setOption(QFileDialog::DontUseNativeDialog, true);   // room for the extra controls
	dlg.setDefaultSuffix(QStringLiteral("tif"));

	// ---- CreateExtraCaptureControl
	auto* panel = new QGroupBox(QObject::tr("Additional Options"), &dlg);
	auto* group1 = new QVBoxLayout(panel);

	//compressed
	auto* ch1 = new QCheckBox(QObject::tr("Lempel-Ziv-Welch Compression"), panel);
	ch1->setChecked(hooks.get_compress ? hooks.get_compress() : true);
	QObject::connect(ch1, &QCheckBox::toggled, panel, [&hooks](bool on) {
		if (hooks.set_compress) hooks.set_compress(on);   // wx OnCh1Check
	});
	group1->addWidget(ch1);

	auto* sizer1 = new QHBoxLayout();
	auto* vald_int = new QIntValidator(0, 1000000000, panel);   // wxIntegerValidator<unsigned int>
	auto* cap_w_txt = new QLineEdit(QString::number(cap_resx), panel);
	cap_w_txt->setValidator(vald_int);
	cap_w_txt->setAlignment(Qt::AlignRight);
	cap_w_txt->setFixedWidth(60);
	auto* cap_h_txt = new QLineEdit(QString::number(cap_resy), panel);
	cap_h_txt->setValidator(vald_int);
	cap_h_txt->setAlignment(Qt::AlignRight);
	cap_h_txt->setFixedWidth(60);
	QObject::connect(cap_w_txt, &QLineEdit::textChanged, panel, [&cap_resx](const QString& t) { cap_resx = t.toInt(); });
	QObject::connect(cap_h_txt, &QLineEdit::textChanged, panel, [&cap_resy](const QString& t) { cap_resy = t.toInt(); });
	sizer1->addWidget(new QLabel(QObject::tr("width:"), panel));
	sizer1->addSpacing(10);
	sizer1->addWidget(cap_w_txt);
	sizer1->addSpacing(20);
	sizer1->addWidget(new QLabel(QObject::tr("height:"), panel));
	sizer1->addSpacing(10);
	sizer1->addWidget(cap_h_txt);
	sizer1->addSpacing(20);
	auto* orgresbtn = new QPushButton(QObject::tr("1:1"), panel);
	orgresbtn->setFixedWidth(48);
	orgresbtn->setAutoDefault(false);
	QObject::connect(orgresbtn, &QPushButton::clicked, panel, [=]() {   // wx OnSetOrgResButton
		cap_w_txt->setText(QString::number(cap_orgresx));
		cap_h_txt->setText(QString::number(cap_orgresy));
	});
	sizer1->addWidget(orgresbtn);
	sizer1->addSpacing(5);
	auto* dispresbtn = new QPushButton(QObject::tr("Disp"), panel);
	dispresbtn->setFixedWidth(48);
	dispresbtn->setAutoDefault(false);
	QObject::connect(dispresbtn, &QPushButton::clicked, panel, [=]() {   // wx OnSetDispResButton
		cap_w_txt->setText(QString::number(cap_dispresx));
		cap_h_txt->setText(QString::number(cap_dispresy));
	});
	sizer1->addWidget(dispresbtn);
	sizer1->addStretch(1);
	group1->addLayout(sizer1);

	//copy all files check box
	if (opt.proj_save)
	{
		auto* ch_embed = new QCheckBox(QObject::tr("Embed all files in the project folder"), panel);
		ch_embed->setChecked(hooks.get_embed ? hooks.get_embed() : false);
		QObject::connect(ch_embed, &QCheckBox::toggled, panel, [&hooks](bool on) {
			if (hooks.set_embed) hooks.set_embed(on);   // wx OnChEmbedCheck
		});
		group1->addWidget(ch_embed);
	}

	//capture each group separately
	auto* ch_group = new QCheckBox(QObject::tr("Capture each group separately"), panel);
	ch_group->setChecked(s_cap_group_each);
	QObject::connect(ch_group, &QCheckBox::toggled, panel, [](bool on) { s_cap_group_each = on; });
	group1->addWidget(ch_group);

	if (auto* grid = qobject_cast<QGridLayout*>(dlg.layout()))
		grid->addWidget(panel, grid->rowCount(), 0, 1, grid->columnCount());
	else if (dlg.layout())
		dlg.layout()->addWidget(panel);

	// testing: VVD_QT_GRAB_CAPDLG=<file.png> saves a picture of this dialog and cancels it
	const QString grab_dlg = qEnvironmentVariable("VVD_QT_GRAB_CAPDLG");
	if (!grab_dlg.isEmpty())
		QTimer::singleShot(1500, &dlg, [&dlg, grab_dlg]() { dlg.grab().save(grab_dlg); dlg.reject(); });
	if (dlg.exec() != QDialog::Accepted)
		return false;
	const QStringList files = dlg.selectedFiles();
	if (files.isEmpty() || files.first().isEmpty())
		return false;

	opt.resx = cap_resx;
	opt.resy = cap_resy;
	opt.compress = ch1->isChecked();
	opt.group_each = s_cap_group_each;
	Execute(parent, canvas, hooks, files.first(), opt);
	return true;
}

void CaptureDialog::Execute(QWidget* parent, RenderCanvas* canvas, const Hooks& hooks,
	const QString& file, const Options& opt)
{
	if (!canvas || file.isEmpty()) return;

	if (opt.group_each)
	{
		// wx StartGroupCapture: only the directory of the chosen file is
		// used; one "<first volume name>.tif" per volume group with volumes
		// (groups named "background" excluded; they stay visible in every
		// capture)
		const QString dir = QFileInfo(file).absolutePath();
		std::vector<RenderCanvas::GroupCaptureEntry> queue;
		std::vector<DataGroup*> groups;
		for (int i = 0; i < canvas->GetLayerNum(); i++)
		{
			TreeLayer* layer = canvas->GetLayer(i);
			if (layer && layer->IsA() == 5)
			{
				DataGroup* g = (DataGroup*)layer;
				if (g->GetVolumeNum() > 0 && QString::fromStdWString(g->GetName()).toLower() != "background")
					groups.push_back(g);
			}
		}
		std::map<QString, int> name_count;
		for (DataGroup* g : groups)
		{
			VolumeData* first_vd = g->GetVolumeData(0);
			if (!first_vd) continue;
			const QString full = first_vd->GetName().ToQt();
			QString name = QFileInfo(full).completeBaseName();   // wxFileName::GetName
			if (name.isEmpty())
				name = full;
			name_count[name]++;
			if (name_count[name] > 1)
				name = name + QString("_%1").arg(name_count[name] - 1);
			RenderCanvas::GroupCaptureEntry e;
			e.filename = dir + "/" + name + ".tif";
			e.group = g;
			queue.push_back(e);
		}
		if (queue.empty())
		{
			QMessageBox::information(parent, QObject::tr("Capture"), QObject::tr("No volume groups found."));
			return;
		}
		canvas->StartGroupCapture(queue, opt.compress);
		return;
	}

	canvas->StartCapture(file, opt.resx, opt.resy, opt.compress);

	if (opt.proj_save && hooks.save_project)
	{
		// wx: new_folder = m_cap_file + "_project"; CREATE_DIR(new_folder);
		//     prop_file = new_folder + "/" + file_dlg.GetFilename() + "_project.vrp"
		const QString new_folder = file + "_project";
		QDir().mkpath(new_folder);
		const QString prop_file = new_folder + "/" + QFileInfo(file).fileName() + "_project.vrp";
		hooks.save_project(prop_file);
	}
}
