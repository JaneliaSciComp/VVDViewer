#include "FileDialogs.h"

#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QCheckBox>
#include <QRadioButton>
#include <QLineEdit>
#include <QLabel>
#include <QIntValidator>
#include <QTimer>
#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QApplication>
#include <QMouseEvent>

namespace
{
	// wx DataTreeCtrl statics
	bool s_md_save_indv = false;       // m_md_save_indv
	bool s_crop_output_image = false;  // m_crop_output_image
	int s_save_scale_level = 0;        // m_save_scale_level

	QString EnvFor(const char* prefix, const char* name)
	{
		return qEnvironmentVariable((QByteArray(prefix) + QByteArray(name).toUpper()).constData());
	}

	bool TestHooksActive(const char* name)
	{
		return qEnvironmentVariableIsSet("VVD_QT_GRAB_FILEDLG") ||
			!EnvFor("VVD_QT_FILEDLG_FILE_", name).isEmpty() ||
			!EnvFor("VVD_QT_FILEDLG_OPTS_", name).isEmpty();
	}

	// the options box at the bottom of the dialog (wx extra control)
	void AddOptions(QFileDialog& dlg, QWidget* panel)
	{
		if (auto* grid = qobject_cast<QGridLayout*>(dlg.layout()))
			grid->addWidget(panel, grid->rowCount(), 0, 1, grid->columnCount());
		else if (dlg.layout())
			dlg.layout()->addWidget(panel);
	}

	void PrepareDialog(QFileDialog& dlg, bool save)
	{
		dlg.setOption(QFileDialog::DontUseNativeDialog, true);   // room for the extra controls
		dlg.setAcceptMode(save ? QFileDialog::AcceptSave : QFileDialog::AcceptOpen);
	}
}

namespace
{
	// While a scripted dialog runs, mouse input from the window system is
	// ignored (a pointer resting over the dialog must not change its options)
	class BlockMouse : public QObject
	{
	public:
		explicit BlockMouse(QWidget* dlg) : m_dlg(dlg) { qApp->installEventFilter(this); }
		~BlockMouse() override { qApp->removeEventFilter(this); }
		bool eventFilter(QObject* o, QEvent* e) override
		{
			switch (e->type())
			{
			case QEvent::MouseButtonPress:
			case QEvent::MouseButtonRelease:
			case QEvent::MouseButtonDblClick:
			case QEvent::Wheel:
				if (auto* w = qobject_cast<QWidget*>(o))
					if (w == m_dlg || m_dlg->isAncestorOf(w))
						return true;
				break;
			default:
				break;
			}
			return false;
		}
	private:
		QWidget* m_dlg;
	};
}

namespace FileDialogs
{

int Exec(QFileDialog& dlg, const char* name)
{
	const QString grab_dir = qEnvironmentVariable("VVD_QT_GRAB_FILEDLG");
	const QString files = EnvFor("VVD_QT_FILEDLG_FILE_", name);
	const QString opts = EnvFor("VVD_QT_FILEDLG_OPTS_", name);
	if (!grab_dir.isEmpty() || !files.isEmpty() || !opts.isEmpty())
	{
		BlockMouse block(&dlg);
		// the directory of the files first: QFileDialog lists it
		// asynchronously, and selectFile only selects listed files
		if (!files.isEmpty())
			dlg.setDirectory(QFileInfo(files.split(';', Qt::SkipEmptyParts).value(0)).absolutePath());
		QPointer<QFileDialog> pd(&dlg);
		const QString pic = grab_dir.isEmpty() ? QString() :
			QDir(grab_dir).filePath(QString("filedlg_%1.png").arg(QString(name).toLower()));
		QTimer::singleShot(800, &dlg, [pd, files, opts, pic, name]() {
			if (!pd) return;
			// options through the controls' own signals (click / edit)
			for (const QString& kv : opts.split(',', Qt::SkipEmptyParts))
			{
				const QString k = kv.section('=', 0, 0).trimmed();
				const QString v = kv.section('=', 1).trimmed();
				if (k == "filter")   // name filter by index (wx filter index)
				{
					const QStringList nf = pd->nameFilters();
					const int i = v.toInt();
					if (i >= 0 && i < nf.size()) pd->selectNameFilter(nf[i]);
				}
				else if (auto* b = pd->findChild<QAbstractButton*>(k))
				{
					const bool on = v != "0";
					if (b->isChecked() != on) b->click();
				}
				else if (auto* e = pd->findChild<QLineEdit*>(k))
					e->setText(v);
				else
					fprintf(stderr, "[filedlg] %s: no option control '%s'\n", name, k.toLocal8Bit().constData());
			}
			if (!pic.isEmpty())
				pd->grab().save(pic);
			if (!files.isEmpty())
			{
				const QStringList list = files.split(';', Qt::SkipEmptyParts);
				// typed into the file name box (selectFile does not change the
				// box while it has the keyboard focus)
				QString text;
				if (list.size() == 1)
					text = list.first();
				else
					for (const QString& f : list) text += "\"" + QFileInfo(f).fileName() + "\" ";
				if (auto* edit = pd->findChild<QLineEdit*>("fileNameEdit"))
					edit->setText(text.trimmed());
				else
					pd->selectFile(text.trimmed());
				if (pd->selectedFiles().isEmpty())
					fprintf(stderr, "[filedlg] %s: could not select %s\n", name, files.toLocal8Bit().constData());
				QMetaObject::invokeMethod(pd.data(), "accept");   // QFileDialog::accept is protected
			}
			else
				pd->reject();
		});
		return dlg.exec();
	}
	return dlg.exec();
}

// ---------------------------------------------------------------------------
// Open Volume: VRenderFrame::OnOpenVolume / CreateExtraControlVolume
// ---------------------------------------------------------------------------

QStringList OpenVolume(QWidget* parent, const QString& dir, const QString& filter,
	OpenVolumeOptions& opt, const OpenVolumeHooks& hooks)
{
	// wx: m_compression / m_skip_brick from the settings when the dialog opens
	bool compression = hooks.get_compress ? hooks.get_compress() : false;
	bool skip_brick = hooks.get_skip_brick ? hooks.get_skip_brick() : false;

	QFileDialog dlg(parent, QObject::tr("Choose the volume data file"), dir, filter);
	PrepareDialog(dlg, false);
	dlg.setFileMode(QFileDialog::ExistingFiles);   // wxFD_OPEN|wxFD_MULTIPLE
	// Zarr roots are opened through their .zgroup / .zattrs (wx filter *.zgroup)
	dlg.setFilter(dlg.filter() | QDir::Hidden);

	auto* panel = new QGroupBox(QObject::tr("Additional Options"), &dlg);
	auto* group1 = new QVBoxLayout(panel);

	//slice sequence check box
	auto* ch1 = new QRadioButton(QObject::tr(
		"Read a sequence as Z slices (the last digits in filenames are used to identify the sequence)"), panel);
	ch1->setObjectName("slice_seq");
	//time sequence check box
	auto* ch4 = new QRadioButton(QObject::tr(
		"Read a sequence as time series (the last digits in filenames are used to identify the sequence)"), panel);
	ch4->setObjectName("time_seq");
	// wx OnCh1Click / OnCh4Click: one of the two or none; clicking the
	// selected one clears it
	ch1->setAutoExclusive(false);
	ch4->setAutoExclusive(false);
	ch1->setChecked(opt.slice_seq);
	ch4->setChecked(opt.time_seq);
	QObject::connect(ch1, &QRadioButton::clicked, panel, [&opt, ch1, ch4](bool on) {
		opt.slice_seq = on;
		if (on) { opt.time_seq = false; ch4->setChecked(false); }
		ch1->setChecked(opt.slice_seq);
	});
	QObject::connect(ch4, &QRadioButton::clicked, panel, [&opt, ch1, ch4](bool on) {
		opt.time_seq = on;
		if (on) { opt.slice_seq = false; ch1->setChecked(false); }
		ch4->setChecked(opt.time_seq);
	});

	//compression
	auto* ch2 = new QCheckBox(QObject::tr(
		"Compress data (loading will take longer time and data are compressed in graphics memory)"), panel);
	ch2->setObjectName("compress");
	ch2->setChecked(compression);
	QObject::connect(ch2, &QCheckBox::toggled, panel, [&compression](bool on) { compression = on; });   // wx OnCh2Check

	//empty brick skipping
	auto* ch3 = new QCheckBox(QObject::tr("Skip empty bricks during rendering (loading takes longer time)"), panel);
	ch3->setObjectName("skip_brick");
	ch3->setChecked(skip_brick);
	QObject::connect(ch3, &QCheckBox::toggled, panel, [&skip_brick](bool on) { skip_brick = on; });   // wx OnCh3Check

	//time sequence identifier
	auto* sizer1 = new QHBoxLayout();
	auto* txt1 = new QLineEdit(opt.time_id, panel);
	txt1->setObjectName("time_id");
	txt1->setFixedWidth(80);
	QObject::connect(txt1, &QLineEdit::textChanged, panel, [&opt](const QString& t) { opt.time_id = t; });   // wx OnTxt1Change
	sizer1->addWidget(txt1);
	sizer1->addSpacing(10);
	sizer1->addWidget(new QLabel(QObject::tr(
		"Sequence identifier (digits after the identifier in filenames are used as index)"), panel));
	sizer1->addStretch(1);

	group1->addWidget(ch1);
	group1->addWidget(ch4);
	group1->addWidget(ch2);
	group1->addWidget(ch3);
	group1->addLayout(sizer1);
	AddOptions(dlg, panel);

	if (Exec(dlg, "OPENVOLUME") != QDialog::Accepted)
		return QStringList();
	const QStringList files = dlg.selectedFiles();
	// wx: written back to the settings after LoadVolumes (OK only)
	if (hooks.set_compress) hooks.set_compress(compression);
	if (hooks.set_skip_brick) hooks.set_skip_brick(skip_brick);
	if (hooks.update_settings_ui) hooks.update_settings_ui();
	return files;
}

// ---------------------------------------------------------------------------
// Save Volume: DataTreeCtrl::CreateExtraControl (+ OnCh1Check, OnCh2Check,
// OnTxt1Change)
// ---------------------------------------------------------------------------

bool GetMetadataSeparate() { return s_md_save_indv; }
bool GetCropToClip() { return s_crop_output_image; }
int GetScaleLevel() { return s_save_scale_level; }

QString SaveVolume(QWidget* parent, const char* name, const QString& title, const QStringList& filters,
	int* filter_index, const QString& default_file)
{
	QFileDialog dlg(parent, title, QString(), filters.join(";;"));
	PrepareDialog(dlg, true);
	dlg.setFileMode(QFileDialog::AnyFile);   // wxFD_SAVE|wxFD_OVERWRITE_PROMPT
	if (!default_file.isEmpty())
		dlg.selectFile(default_file);

	auto* panel = new QGroupBox(QObject::tr("Additional Options"), &dlg);
	auto* group1 = new QVBoxLayout(panel);

	auto* ch1 = new QCheckBox(QObject::tr("Save metadata as an individual file"), panel);
	ch1->setObjectName("md_save_indv");
	ch1->setChecked(s_md_save_indv);
	QObject::connect(ch1, &QCheckBox::toggled, panel, [](bool on) { s_md_save_indv = on; });

	auto* ch2 = new QCheckBox(QObject::tr("Crop image using clipping planes"), panel);
	ch2->setObjectName("crop");
	ch2->setChecked(s_crop_output_image);
	QObject::connect(ch2, &QCheckBox::toggled, panel, [](bool on) { s_crop_output_image = on; });

	auto* sizer1 = new QHBoxLayout();
	auto* txt1 = new QLineEdit(QString::number(s_save_scale_level), panel);
	txt1->setObjectName("scale_level");
	txt1->setValidator(new QIntValidator(0, 1000000, txt1));   // wxIntegerValidator<unsigned int>
	txt1->setFixedWidth(34);
	QObject::connect(txt1, &QLineEdit::textChanged, panel, [](const QString& t) { s_save_scale_level = t.toInt(); });   // wxAtoi
	sizer1->addWidget(txt1);
	sizer1->addSpacing(10);
	sizer1->addWidget(new QLabel(QObject::tr("Scale level (for resolution pyramid)"), panel));
	sizer1->addStretch(1);

	group1->addWidget(ch1);
	group1->addWidget(ch2);
	group1->addLayout(sizer1);
	AddOptions(dlg, panel);

	if (Exec(dlg, name) != QDialog::Accepted)
		return QString();
	const QStringList files = dlg.selectedFiles();
	if (files.isEmpty())
		return QString();
	if (filter_index)
		*filter_index = std::max(0, (int)filters.indexOf(dlg.selectedNameFilter()));
	return files.first();
}

// ---------------------------------------------------------------------------
// Save Project: VRenderFrame::OnSaveProject / CreateExtraControlProjectSave
// ---------------------------------------------------------------------------

QString SaveProject(QWidget* parent, const QString& dir, bool& embed,
	const std::function<bool()>& get_compress, const std::function<void(bool)>& set_compress)
{
	QFileDialog dlg(parent, QObject::tr("Save Project File"), dir, QObject::tr("VVDViewer project (*.vrp)"));
	PrepareDialog(dlg, true);
	dlg.setFileMode(QFileDialog::AnyFile);
	dlg.setDefaultSuffix("vrp");

	auto* panel = new QGroupBox(QObject::tr("Additional Options"), &dlg);
	auto* group1 = new QVBoxLayout(panel);

	//copy all files check box
	auto* ch_embed = new QCheckBox(QObject::tr("Embed all files in the project folder"), panel);
	ch_embed->setObjectName("embed");
	ch_embed->setChecked(embed);
	QObject::connect(ch_embed, &QCheckBox::toggled, panel, [&embed](bool on) { embed = on; });   // wx OnChEmbedCheck

	//compressed
	auto* ch_cmp = new QCheckBox(QObject::tr("Compression"), panel);
	ch_cmp->setObjectName("compress");
	ch_cmp->setChecked(get_compress ? get_compress() : true);
	QObject::connect(ch_cmp, &QCheckBox::toggled, panel, [set_compress](bool on) {   // wx OnChSaveCmpCheck
		if (set_compress) set_compress(on);
	});

	group1->addWidget(ch_embed);
	group1->addWidget(ch_cmp);
	AddOptions(dlg, panel);

	if (Exec(dlg, "SAVEPROJECT") != QDialog::Accepted)
		return QString();
	const QStringList files = dlg.selectedFiles();
	return files.isEmpty() ? QString() : files.first();
}

// ---------------------------------------------------------------------------
// plain dialogs: the platform dialog, except under the testing aids
// ---------------------------------------------------------------------------

QStringList OpenFiles(QWidget* parent, const char* name, const QString& title, const QString& dir, const QString& filter)
{
	if (!TestHooksActive(name))
		return QFileDialog::getOpenFileNames(parent, title, dir, filter);
	QFileDialog dlg(parent, title, dir, filter);
	PrepareDialog(dlg, false);
	dlg.setFileMode(QFileDialog::ExistingFiles);
	if (Exec(dlg, name) != QDialog::Accepted)
		return QStringList();
	return dlg.selectedFiles();
}

QString OpenFile(QWidget* parent, const char* name, const QString& title, const QString& dir, const QString& filter)
{
	if (!TestHooksActive(name))
		return QFileDialog::getOpenFileName(parent, title, dir, filter);
	QFileDialog dlg(parent, title, dir, filter);
	PrepareDialog(dlg, false);
	dlg.setFileMode(QFileDialog::ExistingFile);
	if (Exec(dlg, name) != QDialog::Accepted)
		return QString();
	const QStringList files = dlg.selectedFiles();
	return files.isEmpty() ? QString() : files.first();
}

}

// ---------------------------------------------------------------------------
// wx DatasetSelectionDialog / DatasetListCtrl (VRenderFrame.cpp)
// ---------------------------------------------------------------------------

#include <QDialog>
#include <QListWidget>
#include <QPushButton>

namespace FileDialogs
{

bool SelectDatasets(QWidget* parent, const QString& title, const QStringList& list, std::vector<bool>& selected)
{
	QDialog dlg(parent);
	dlg.setWindowTitle(title);
	dlg.resize(500, 600);
	auto* top = new QVBoxLayout(&dlg);
	top->addSpacing(5);
	auto* st = new QLabel(QObject::tr("Choose Zarr/N5 datasets"), &dlg);
	top->addWidget(st, 0, Qt::AlignHCenter);
	top->addSpacing(10);
	auto* lst = new QListWidget(&dlg);
	lst->setObjectName("datasetList");
	lst->setMinimumSize(480, 500);
	lst->setSelectionMode(QAbstractItemView::NoSelection);   // wx OnItemSelected clears the selection
	for (const QString& name : list)
	{
		auto* it = new QListWidgetItem(name, lst);
		it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
		it->setCheckState(Qt::Unchecked);
	}
	// wx DatasetListCtrl::OnLeftDown: a click anywhere on a row toggles it
	QObject::connect(lst, &QListWidget::itemClicked, &dlg, [](QListWidgetItem* it) {
		it->setCheckState(it->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
	});
	top->addWidget(lst, 1);
	top->addSpacing(10);
	auto* row2 = new QHBoxLayout;
	auto* sel_all = new QPushButton(QObject::tr("Select All"), &dlg);
	auto* desel_all = new QPushButton(QObject::tr("Deselect All"), &dlg);
	sel_all->setObjectName("selectAll");
	desel_all->setObjectName("deselectAll");
	row2->addSpacing(5);
	row2->addWidget(sel_all);
	row2->addSpacing(5);
	row2->addWidget(desel_all);
	row2->addStretch(1);
	top->addLayout(row2);
	top->addSpacing(10);
	auto* row = new QHBoxLayout;
	auto* ok = new QPushButton(QObject::tr("OK"), &dlg);
	auto* cancel = new QPushButton(QObject::tr("Cancel"), &dlg);
	ok->setDefault(true);
	row->addStretch(1);
	row->addWidget(ok);
	row->addSpacing(5);
	row->addWidget(cancel);
	row->addSpacing(10);
	top->addLayout(row);
	top->addSpacing(10);
	auto set_all = [lst](bool on) {
		for (int i = 0; i < lst->count(); ++i)
			lst->item(i)->setCheckState(on ? Qt::Checked : Qt::Unchecked);
	};
	QObject::connect(sel_all, &QPushButton::clicked, &dlg, [set_all]() { set_all(true); });
	QObject::connect(desel_all, &QPushButton::clicked, &dlg, [set_all]() { set_all(false); });
	QObject::connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
	QObject::connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);

	const QString answer = qEnvironmentVariable("VVD_QT_DATASETS");
	const QString grab_dir = qEnvironmentVariable("VVD_QT_GRAB_FILEDLG");
	if (!answer.isEmpty() || !grab_dir.isEmpty())
	{
		QPointer<QDialog> pd(&dlg);
		QTimer::singleShot(800, &dlg, [pd, answer, grab_dir, lst, sel_all, desel_all, ok, cancel]() {
			if (!pd) return;
			if (answer == "all") sel_all->click();
			else if (answer == "none") desel_all->click();
			else if (answer != "cancel")
				for (const QString& s : answer.split(',', Qt::SkipEmptyParts))
				{
					const int i = s.trimmed().toInt();
					if (i >= 0 && i < lst->count())
						Q_EMIT lst->itemClicked(lst->item(i));   // the mouse path
				}
			if (!grab_dir.isEmpty())
				pd->grab().save(QDir(grab_dir).filePath("filedlg_datasets.png"));
			if (answer == "cancel" || answer.isEmpty()) cancel->click();
			else ok->click();
		});
	}
	if (dlg.exec() != QDialog::Accepted)
		return false;
	selected.assign(lst->count(), false);
	for (int i = 0; i < lst->count(); ++i)
		selected[i] = lst->item(i)->checkState() == Qt::Checked;
	return true;
}

}
