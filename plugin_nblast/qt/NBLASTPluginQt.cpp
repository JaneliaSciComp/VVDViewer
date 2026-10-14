#include "NBLASTPluginQt.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QRadioButton>
#include <QButtonGroup>
#include <QPushButton>
#include <QToolBar>
#include <QAction>
#include <QSplitter>
#include <QGroupBox>
#include <QFrame>
#include <QTreeWidget>
#include <QHeaderView>
#include <QScrollBar>
#include <QMessageBox>
#include <QFileDialog>
#include <QDialogButtonBox>
#include <QIntValidator>
#include <QProgressDialog>
#include <QProcess>
#include <QPainter>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QTextStream>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QRandomGenerator>
#include <QTimer>
#include <cmath>

#include "TestRun.h"
#include "QtPluginManager.h"
#include "FileDialogs.h"
#include "DataManager.h"
#include "Core/FileUtil.h"
#include "FLIVR/VolumeRenderer.h"
#include "FLIVR/Texture.h"
#include "Formats/msk_writer.h"

vvd::Plugin* CreateNBLASTPluginQt()
{
	return new NBLASTPluginQt();
}

bool NBLASTPluginQt::m_exp_swc = false;
bool NBLASTPluginQt::m_exp_swcprev = false;
bool NBLASTPluginQt::m_exp_mip = false;
bool NBLASTPluginQt::m_exp_vol = false;
bool NBLASTPluginQt::m_pfx_score = true;
bool NBLASTPluginQt::m_pfx_db = true;

namespace
{
	void Report(QWidget* parent, const QString& msg, const QString& title = "NBLAST Plugin")
	{
		if (VVDQtScriptedRun())
			fprintf(stderr, "[nblast] %s: %s\n", title.toLocal8Bit().constData(), msg.toLocal8Bit().constData());
		else
			QMessageBox::information(parent, title, msg);
	}
	QString TempDir()
	{
		// wxStandardPaths::GetTempDir
		return QDir::cleanPath(QString::fromStdWString(vvd::GetTempDir()));
	}
	const QChar kSep('/');
	// wxString::BeforeLast(c) ("" when c is not found)
	QString BeforeLast(const QString& s, QChar c)
	{
		const int i = s.lastIndexOf(c);
		return i < 0 ? QString() : s.left(i);
	}
	// wxCopyFile(src, dst) (overwrite)
	bool CopyOver(const QString& src, const QString& dst)
	{
		if (!QFileInfo::exists(src)) return false;
		if (QFileInfo::exists(dst) || QFileInfo(dst).isSymLink()) QFile::remove(dst);
		return QFile::copy(src, dst);
	}
	// wxStringTokenizer(str, delims) in wxTOKEN_DEFAULT mode: empty tokens skipped
	QStringList Tokenize(const QString& s, const QString& delims)
	{
		QStringList out;
		QString cur;
		for (QChar c : s)
		{
			if (delims.contains(c)) { if (!cur.isEmpty()) out << cur; cur.clear(); }
			else cur += c;
		}
		if (!cur.isEmpty()) out << cur;
		return out;
	}
	// QFileDialog with the wx extra control box (FileDialogs.cpp AddOptions)
	void AddOptions(QFileDialog& dlg, QWidget* panel)
	{
		if (auto* grid = qobject_cast<QGridLayout*>(dlg.layout()))
			grid->addWidget(panel, grid->rowCount(), 0, 1, grid->columnCount());
		else if (dlg.layout())
			dlg.layout()->addWidget(panel);
	}
	QString PickFile(QWidget* parent, const char* name, const QString& title, const QString& dir, const QString& filter, bool save = false)
	{
		QFileDialog dlg(parent, title, dir, filter);
		dlg.setOption(QFileDialog::DontUseNativeDialog, true);
		dlg.setAcceptMode(save ? QFileDialog::AcceptSave : QFileDialog::AcceptOpen);
		dlg.setFileMode(save ? QFileDialog::AnyFile : QFileDialog::ExistingFile);
		if (FileDialogs::Exec(dlg, name) != QDialog::Accepted) return QString();
		const QStringList f = dlg.selectedFiles();
		return f.isEmpty() ? QString() : f.first();
	}
	// a wxFilePickerCtrl / wxDirPickerCtrl: line edit + Browse
	QWidget* MakePicker(QWidget* parent, QLineEdit*& edit, const QString& value, const QString& caption,
		const QString& filter, bool dir)
	{
		auto* w = new QWidget(parent);
		auto* l = new QHBoxLayout(w);
		l->setContentsMargins(0, 0, 0, 0);
		edit = new QLineEdit(value, w);
		auto* b = new QPushButton(QObject::tr("Browse..."), w);
		l->addWidget(edit, 1);
		l->addWidget(b);
		QLineEdit* e = edit;
		QObject::connect(b, &QPushButton::clicked, w, [w, e, caption, filter, dir]() {
			const QString f = dir ? QFileDialog::getExistingDirectory(w, caption, e->text()) :
				QFileDialog::getOpenFileName(w, caption, QFileInfo(e->text()).absolutePath(), filter);
			if (!f.isEmpty()) e->setText(f);
		});
		return w;
	}
}

// ---------------------------------------------------------------------------
// plugin (wx NBLASTGuiPlugin)
// ---------------------------------------------------------------------------

NBLASTPluginQt::NBLASTPluginQt()
{
}

NBLASTPluginQt::~NBLASTPluginQt()
{
}

QWidget* NBLASTPluginQt::CreatePanel(QWidget* parent)
{
	return new NBLASTPanel(this, parent);
}

// <bundle>/Contents/Resources/<name> on macOS, else next to the executable
// or in the user config dir (wx Load/SaveConfigFile, LoadList / SaveList)
QString NBLASTPluginQt::ResourcePath(const QString& name, bool for_save)
{
	const QString expath = QCoreApplication::applicationDirPath();
#ifdef __APPLE__
	(void)for_save;
	return expath + "/../Resources/" + name;
#else
	QString dft = expath + "/" + name;
	const QString dft2 = QString::fromStdWString(vvd::GetUserConfigDir()) + "/" + name;
	if (for_save) { if (!QFileInfo::exists(dft) && QFileInfo::exists(dft2)) dft = dft2; }
	else if (!QFileInfo::exists(dft)) dft = dft2;
	return dft;
#endif
}

QString NBLASTPluginQt::RScriptPath()
{
	const QString test = qEnvironmentVariable("VVD_QT_NBLAST_RSCRIPT");
	if (!test.isEmpty()) return test;
	const QString expath = QCoreApplication::applicationDirPath();
#ifdef __APPLE__
	return expath + "/../Resources/nblast_search.R";
#else
	return expath + "/nblast_search.R";
#endif
}

void NBLASTPluginQt::OnInit()
{
	LoadConfigFile();
#ifdef _WIN32
	// wx: the newest R-* under %ProgramW6432%\R, else %programfiles(x86)%\R
	if (m_R_path.isEmpty() || !QFileInfo::exists(m_R_path))
	{
		QString rpath;
		for (const char* env : { "ProgramW6432", "ProgramFiles(x86)" })
		{
			const QString rdir = qEnvironmentVariable(env) + "\\R";
			QDir d(rdir);
			if (!d.exists()) continue;
			QStringList list = d.entryList(QStringList() << "R-*", QDir::Dirs, QDir::Name);
			for (const QString& sub : list)
			{
				QDir bin(rdir + "\\" + sub + "\\bin");
				const QStringList r = bin.entryList(QStringList() << "*Rscript*", QDir::Files);
				if (!r.isEmpty()) { rpath = bin.filePath(r.first()); break; }
			}
			if (!rpath.isEmpty()) break;
		}
		m_R_path = rpath;
	}
#else
	if ((m_R_path.isEmpty() || !QFileInfo::exists(m_R_path)) &&
		QFileInfo::exists("/Library/Frameworks/R.framework/Resources/bin/Rscript"))
		m_R_path = "/Library/Frameworks/R.framework/Resources/bin/Rscript";
#endif
	if (m_out_dir.isEmpty() || !QFileInfo(m_out_dir).isDir())
		m_out_dir = TempDir();
	if (m_rnum.isEmpty())
		m_rnum = "100";
	if (m_scmtd.isEmpty())
		m_scmtd = "forward";
	// testing aids
	const QString od = qEnvironmentVariable("VVD_QT_NBLAST_OUTDIR");
	if (!od.isEmpty()) m_out_dir = od;
}

void NBLASTPluginQt::LoadConfigFile()
{
	const QString dft = ResourcePath("NBLAST_plugin_settings.dft", false);
	if (!QFileInfo::exists(dft)) return;
	vvd::IniFile fconfig;
	if (!fconfig.Load(VVDString(dft))) return;
	VVDString str;
	if (fconfig.Read(L"R_path", str)) m_R_path = str.ToQt();
	if (fconfig.Read(L"neuronlib_path", str)) m_nlib_path = str.ToQt();
	if (fconfig.Read(L"output_dir", str)) m_out_dir = str.ToQt();
	if (fconfig.Read(L"rnum", str)) m_rnum = str.ToQt();
	else m_rnum = "10";
	bool bval = false;
	if (fconfig.Read(L"export_swc", bval)) m_exp_swc = bval;
	if (fconfig.Read(L"export_swcprev", bval)) m_exp_swcprev = bval;
	if (fconfig.Read(L"export_mip", bval)) m_exp_mip = bval;
	if (fconfig.Read(L"export_vol", bval)) m_exp_vol = bval;
	if (fconfig.Read(L"prefix_score", bval)) m_pfx_score = bval;
	if (fconfig.Read(L"prefix_database", bval)) m_pfx_db = bval;
	if (fconfig.Read(L"scoring_method", str)) m_scmtd = str.ToQt();
}

void NBLASTPluginQt::SaveConfigFile()
{
	if (VVDQtScriptedRun()) return;   // TestRun.h
	vvd::IniFile fconfig;
	fconfig.Write(L"R_path", VVDString(m_R_path));
	fconfig.Write(L"neuronlib_path", VVDString(m_nlib_path));
	fconfig.Write(L"output_dir", VVDString(m_out_dir));
	fconfig.Write(L"rnum", VVDString(m_rnum));
	fconfig.Write(L"export_swc", m_exp_swc);
	fconfig.Write(L"export_swcprev", m_exp_swcprev);
	fconfig.Write(L"export_mip", m_exp_mip);
	fconfig.Write(L"export_vol", m_exp_vol);
	fconfig.Write(L"prefix_score", m_pfx_score);
	fconfig.Write(L"prefix_database", m_pfx_db);
	fconfig.Write(L"scoring_method", VVDString(m_scmtd));
	fconfig.Save(VVDString(ResourcePath("NBLAST_plugin_settings.dft", true)));
}

bool NBLASTPluginQt::runNBLAST()
{
	return runNBLAST(m_R_path, m_nlib_path, m_out_dir, m_ofname, m_rnum, m_db_names);
}

// the query file: the selected volume's mask (else the volume) as
// vvdnbtmpv.nrrd, or the selected mesh as vvdnbtmpsk.swc
bool NBLASTPluginQt::SaveQuery(QString& tempdpath, bool volume_only)
{
	if (!m_host) return false;
	const int type = m_host->GetCurSelType();
	VolumeData* vd = m_host->GetCurSelVol();
	MeshData* md = m_host->GetCurSelMesh();
	tempdpath = TempDir() + kSep;
	QString maskfname = TempDir() + kSep;
	if (vd && (type == 2 || volume_only))
	{
		if (volume_only && (!vd->GetTexture() || !vd->GetVR())) return false;
		tempdpath += "vvdnbtmpv.nrrd";
		maskfname += "vvdnbtmpv.msk";
		if (vd->GetTexture() && vd->GetTexture()->nmask() != -1)
		{
			vd->GetVR()->return_mask();
			auto data = vd->GetTexture()->get_nrrd(vd->GetTexture()->nmask());
			double spcx, spcy, spcz;
			vd->GetSpacings(spcx, spcy, spcz);
			if (data)
			{
				MSKWriter msk_writer;
				msk_writer.SetData(data);
				msk_writer.SetSpacings(spcx, spcy, spcz);
				msk_writer.Save(maskfname.toStdWString(), 0);
				// wxRenameFile(maskfname, tempdpath, true)
				QFile::remove(tempdpath);
				QFile::rename(maskfname, tempdpath);
			}
		}
		else
			vd->Save(VVDString(tempdpath), 2, false, true, true, false);
		return true;
	}
	if (!volume_only && md && type == 3)
	{
		tempdpath += "vvdnbtmpsk.swc";
		CopyOver(md->GetPath().ToQt(), tempdpath);
		return true;
	}
	return false;
}

bool NBLASTPluginQt::RunRScript(const QStringList& args)
{
	m_last_command = QStringList() << m_R_path << args;
	fprintf(stderr, "[nblast] %s\n", m_last_command.join(' ').toLocal8Bit().constData());
	QWidget* parent = m_host ? m_host->PluginParentWidget() : nullptr;
	QProcess proc;
	proc.setProcessChannelMode(QProcess::MergedChannels);
	proc.start(m_R_path, args);
	if (!proc.waitForStarted(10000))
	{
		Report(parent, QString("Could not run %1").arg(m_R_path));
		m_last_exit = -1;
		return false;
	}
	// wx: wxExecute(term.sh ..., wxEXEC_SYNC) -- a Terminal window, waited for
	QProgressDialog prg(QObject::tr("Running NBLAST..."), QObject::tr("Cancel"), 0, 0, parent);
	prg.setObjectName("NBLASTRunProgress");
	prg.setWindowTitle(QObject::tr("NBLAST Plugin"));
	prg.setWindowModality(Qt::ApplicationModal);
	prg.setMinimumDuration(0);
	prg.setAutoClose(false);
	prg.setAutoReset(false);
	prg.show();
	QString last_line;
	while (proc.state() != QProcess::NotRunning)
	{
		proc.waitForReadyRead(50);
		const QByteArray out = proc.readAll();
		if (!out.isEmpty())
		{
			fwrite(out.constData(), 1, out.size(), stderr);
			const QStringList lines = QString::fromLocal8Bit(out).split('\n', Qt::SkipEmptyParts);
			if (!lines.isEmpty()) last_line = lines.last().trimmed();
			if (!last_line.isEmpty()) prg.setLabelText(last_line.left(120));
		}
		QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
		if (prg.wasCanceled())
		{
			proc.kill();
			proc.waitForFinished(3000);
			break;
		}
	}
	const QByteArray rest = proc.readAll();
	if (!rest.isEmpty()) fwrite(rest.constData(), 1, rest.size(), stderr);
	m_last_exit = proc.exitStatus() == QProcess::NormalExit ? proc.exitCode() : -1;
	return true;
}

bool NBLASTPluginQt::runNBLAST(const QString& rpath, const QString& nlibpath, const QString& outdir, const QString& ofname,
	const QString& rnum, const QString& db_names, const QString& scmethod)
{
	if (!QFileInfo::exists(rpath) || outdir.isEmpty() || ofname.isEmpty() || rnum.isEmpty())
		return false;
	if (!m_host) return false;
	QString tempdpath;
	if (!SaveQuery(tempdpath, false))
		return false;

	m_R_path = rpath;
	m_nlib_path = nlibpath;
	m_out_dir = outdir;
	m_ofname = ofname;
	m_rnum = rnum;
	m_db_names = db_names;
	m_scmtd = scmethod;

	QStringList args;
	args << RScriptPath() << tempdpath << m_nlib_path << m_ofname << m_out_dir << m_rnum;
	if (!m_db_names.isEmpty())
	{
		args << m_db_names;
		if (!m_scmtd.isEmpty())
			args << m_scmtd;
	}
	RunRScript(args);
	if (QWidget* w = m_host->PluginParentWidget())
	{
		w->raise();
		w->activateWindow();
	}
	return true;
}

bool NBLASTPluginQt::runNBLASTremote(const QString& url, const QString& usr, const QString& pwd, const QString& nlibpath,
	const QString& outdir, const QString& ofname, const QString& rnum, const QString& db_names, const QString& scmethod)
{
	(void)scmethod;
	if (outdir.isEmpty() || ofname.isEmpty() || rnum.isEmpty())
		return false;
	if (!m_host) return false;
	QString tempvdpath;
	if (!SaveQuery(tempvdpath, true))
		return false;
	m_nlib_path = nlibpath;
	m_out_dir = outdir;
	m_ofname = ofname;
	m_rnum = rnum;
	m_db_names = db_names;
	const QString inurl = url + "opt/NBLAST/input/";
	static const char alphanum[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
	QString randname;
	for (int i = 0; i < 16; ++i)
		randname += QChar(alphanum[QRandomGenerator::global()->bounded((int)sizeof(alphanum) - 1)]);
	const QString upvolname = randname + ".nrrd";
	m_host->PluginUploadFileRemote(inurl, upvolname, tempvdpath, usr, pwd);
	return true;
}

bool NBLASTPluginQt::LoadSWC(const QString& name, const QString& swc_zip_path)
{
	if (!m_host || !QFileInfo::exists(swc_zip_path)) return false;
	QString ename = name;
	if (QFileInfo(ename).suffix() != "swc")
		ename += ".swc";
	std::vector<VVDString> entries;
	if (!vvd::ListZipEntries(VVDString(swc_zip_path), entries)) return false;
	bool found = false;
	for (const VVDString& e : entries)
		if (e.ToQt() == ename) { found = true; break; }
	if (!found) return false;
	const QString tmpdir = TempDir() + "/vvd_nblast_swc";
	std::vector<VVDString> extracted;
	const std::vector<VVDString> exts = { VVDString(L".swc") };
	vvd::UnzipToDir(VVDString(swc_zip_path), VVDString(tmpdir), extracted, true, &exts);
	for (const VVDString& f : extracted)
	{
		const QString p = f.ToQt();
		if (QFileInfo(p).fileName() == QFileInfo(ename).fileName())
		{
			const QString tempswcpath = TempDir() + kSep + QFileInfo(ename).fileName();
			CopyOver(p, tempswcpath);
			m_host->PluginLoadMeshes(QStringList() << tempswcpath);
			QFile::remove(tempswcpath);
		}
	}
	QDir(tmpdir).removeRecursively();
	return true;
}

bool NBLASTPluginQt::LoadFiles(const QString& path)
{
	if (!m_host) return false;
	m_host->PluginStartupLoad(QStringList() << path);
	return true;
}

bool NBLASTPluginQt::skeletonizeMask()
{
	const QString fi_name = "Fiji Interface";
	if (!m_host) return false;
	if (!m_host->PluginExists(fi_name))
	{
		Report(m_host->PluginParentWidget(), "ERROR: Could not found Fiji interface");
		return false;
	}
	if (!m_host->IsCreatedPluginWindow(fi_name))
	{
		m_host->CreatePluginWindow(fi_name);
		m_host->ToggleVisibilityPluginWindow(fi_name, false);
	}
	m_host->RunPlugin(fi_name, "NBLAST Skeletonize;true;false");
	return true;
}

// ---------------------------------------------------------------------------
// wx wxUsrPwdDialog
// ---------------------------------------------------------------------------

NBUsrPwdDialog::NBUsrPwdDialog(QWidget* parent, const QString& title)
	: QDialog(parent)
{
	setWindowTitle(title);
	resize(350, 150);
	auto* form = new QFormLayout();
	m_usrtxt = new QLineEdit(this);
	m_usrtxt->setObjectName("NBUser");
	m_usrtxt->setMinimumWidth(200);
	m_pwdtxt = new QLineEdit(this);
	m_pwdtxt->setObjectName("NBPassword");
	m_pwdtxt->setEchoMode(QLineEdit::Password);
	form->addRow(tr("User:"), m_usrtxt);
	form->addRow(tr("Password:"), m_pwdtxt);
	auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
	auto* l = new QVBoxLayout(this);
	l->addLayout(form);
	l->addWidget(bb);
}

QString NBUsrPwdDialog::GetUserNameText() const { return m_usrtxt->text(); }
QString NBUsrPwdDialog::GetPasswordText() const { return m_pwdtxt->text(); }
void NBUsrPwdDialog::SetUserNameText(const QString& usr) { m_usrtxt->setText(usr); }
void NBUsrPwdDialog::SetPasswordText(const QString& pwd) { m_pwdtxt->setText(pwd); }

// ---------------------------------------------------------------------------
// wx wxDBListDialog / NBLASTDatabaseListCtrl
// ---------------------------------------------------------------------------

NBDBListDialog::NBDBListDialog(QWidget* parent, const QString& title)
	: QDialog(parent)
{
	setObjectName("NBDBListDialog");
	setWindowTitle(title);
	resize(500, 600);
	auto* l = new QVBoxLayout(this);
	auto* row = new QHBoxLayout();
	row->addWidget(new QLabel(tr("NBLAST Database (.rds):"), this));
	row->addWidget(MakePicker(this, m_new_db_pick, QString(), tr("Choose a NBLAST database"), "*.rds", false), 1);
	m_new_db_pick->setObjectName("NBNewDBPath");
	l->addLayout(row);
	m_add_button = new QPushButton(tr("Add Database"), this);
	m_add_button->setObjectName("NBAddDBButton");
	l->addWidget(m_add_button, 0, Qt::AlignHCenter);
	connect(m_add_button, &QPushButton::clicked, this, [this]() {
		// wx OnAddButtonClick
		Add(m_new_db_pick->text(), QString(), true);
	});

	m_listctrl = new QTreeWidget(this);
	m_listctrl->setObjectName("NBDBList");
	m_listctrl->setColumnCount(3);
	m_listctrl->setHeaderLabels({ tr("Name"), tr("Path"), QString() });
	m_listctrl->setRootIsDecorated(false);
	m_listctrl->setSelectionMode(QAbstractItemView::SingleSelection);
	m_listctrl->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
	m_listctrl->header()->setStretchLastSection(false);
	m_listctrl->header()->setSectionResizeMode(1, QHeaderView::Stretch);
	m_listctrl->setColumnWidth(0, 150);
	m_listctrl->setColumnWidth(2, 70);
	// wx OnBeginDrag / OnDragging: drag a row to reorder
	m_listctrl->setDragDropMode(QAbstractItemView::InternalMove);
	m_listctrl->setDefaultDropAction(Qt::MoveAction);
	m_listctrl->installEventFilter(this);
	m_listctrl->viewport()->installEventFilter(this);
	l->addWidget(m_listctrl, 1);
	connect(m_listctrl, &QTreeWidget::itemChanged, this, &NBDBListDialog::OnItemChanged);
	connect(m_listctrl, &QTreeWidget::itemSelectionChanged, this, &NBDBListDialog::PlaceSettingButton);
	// the order after a drag: rebuild m_list from the rows
	connect(m_listctrl->model(), &QAbstractItemModel::rowsMoved, this, [this]() {
		if (m_updating) return;
		std::vector<NBLASTDBListItemData> nl;
		for (int i = 0; i < m_listctrl->topLevelItemCount(); i++)
		{
			const int id = m_listctrl->topLevelItem(i)->data(0, Qt::UserRole).toInt();
			if (id >= 0 && id < (int)m_list.size()) nl.push_back(m_list[id]);
		}
		if (nl.size() == m_list.size()) m_list = nl;
		UpdateList();
	});

	// wx m_setting_b: a "Setting" button on the selected row (user / password),
	// PlaceSettingButton

	auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(bb, &QDialogButtonBox::accepted, this, [this]() { SaveList(); accept(); });   // wx OnOk
	connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
	l->addWidget(bb);
	LoadList();
}

QString NBDBListDialog::ListPath(bool for_save)
{
	// testing aid: VVD_QT_NBLAST_DBLIST=<file> (the list to read)
	const QString test = qEnvironmentVariable("VVD_QT_NBLAST_DBLIST");
	if (!test.isEmpty() && !for_save) return test;
	return NBLASTPluginQt::ResourcePath("NBLAST_database_list.txt", for_save);
}

void NBDBListDialog::UpdateList()
{
	m_updating = true;
	m_listctrl->clear();
	for (int i = 0; i < (int)m_list.size(); i++)
	{
		auto* it = new QTreeWidgetItem(m_listctrl);
		it->setText(0, m_list[i].name);
		it->setText(1, m_list[i].path);
		it->setData(0, Qt::UserRole, i);
		it->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable | Qt::ItemIsDragEnabled);
	}
	m_updating = false;
	PlaceSettingButton();
}

void NBDBListDialog::Add(const QString& path, const QString& name, bool selection)
{
	// wx Append: the name is the file name without extension when empty
	NBLASTDBListItemData data;
	data.name = name.isEmpty() ? QFileInfo(path).completeBaseName() : name;
	data.path = path;
	data.state = false;
	m_list.push_back(data);
	UpdateList();
	if (selection && m_listctrl->topLevelItemCount() > 0)
		m_listctrl->setCurrentItem(m_listctrl->topLevelItem(m_listctrl->topLevelItemCount() - 1));
}

void NBDBListDialog::DeleteSelection()
{
	QTreeWidgetItem* it = m_listctrl->currentItem();
	if (!it || !it->isSelected()) return;
	const int item = m_listctrl->indexOfTopLevelItem(it);
	if (item >= 0 && item < (int)m_list.size())
	{
		m_list.erase(m_list.begin() + item);
		UpdateList();
	}
}

void NBDBListDialog::DeleteAll()
{
	m_list.clear();
	UpdateList();
}

void NBDBListDialog::OnItemChanged(QTreeWidgetItem* item, int column)
{
	if (m_updating || !item) return;
	const int i = m_listctrl->indexOfTopLevelItem(item);
	if (i < 0 || i >= (int)m_list.size()) return;
	// wx EndEdit: name / path from the edit controls
	if (column == 0) m_list[i].name = item->text(0);
	if (column == 1) m_list[i].path = item->text(1);
}

void NBDBListDialog::PlaceSettingButton()
{
	// wx: m_setting_b shown in column 2 of the selected row only
	for (int r = 0; r < m_listctrl->topLevelItemCount(); r++)
		if (m_listctrl->itemWidget(m_listctrl->topLevelItem(r), 2))
			m_listctrl->removeItemWidget(m_listctrl->topLevelItem(r), 2);
	m_setting_b = nullptr;
	QTreeWidgetItem* it = m_listctrl->currentItem();
	if (!it || !it->isSelected()) return;
	m_setting_b = new QPushButton(tr("Setting"));
	m_setting_b->setObjectName("NBDBSettingButton");
	connect(m_setting_b, &QPushButton::clicked, this, &NBDBListDialog::OnSettingButton);
	m_listctrl->setItemWidget(it, 2, m_setting_b);
}

void NBDBListDialog::OnSettingButton()
{
	NBUsrPwdDialog updlg(this, tr("Set User/Password"));
	QTreeWidgetItem* it = m_listctrl->currentItem();
	const int item = it ? m_listctrl->indexOfTopLevelItem(it) : -1;
	if (item >= 0 && item < (int)m_list.size())
	{
		updlg.SetUserNameText(m_list[item].usr);
		updlg.SetPasswordText(m_list[item].pwd);
	}
	if (updlg.exec() == QDialog::Accepted && item >= 0 && item < (int)m_list.size())
	{
		m_list[item].usr = updlg.GetUserNameText();
		m_list[item].pwd = updlg.GetPasswordText();
	}
}

bool NBDBListDialog::eventFilter(QObject* o, QEvent* e)
{
	if (o == m_listctrl && e->type() == QEvent::KeyPress)
	{
		auto* ke = static_cast<QKeyEvent*>(e);
		// wx OnKeyDown: Delete / Backspace (not while a cell is edited)
		if (ke->key() == Qt::Key_Delete || ke->key() == Qt::Key_Backspace)
		{
			DeleteSelection();
			return true;
		}
	}
	return QDialog::eventFilter(o, e);
}

void NBDBListDialog::LoadList()
{
	const QString listpath = ListPath(false);
	QFile f(listpath);
	if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
	m_list.clear();
	QTextStream ts(&f);
	while (!ts.atEnd())
	{
		const QString line = ts.readLine();
		const QStringList tokens = Tokenize(line, "\t");
		NBLASTDBListItemData d;
		if (tokens.size() == 1)
		{
			d.path = tokens[0];
			d.name = QFileInfo(tokens[0]).completeBaseName();
			m_list.push_back(d);
		}
		if (tokens.size() >= 2)
		{
			d.name = tokens[0];
			d.path = tokens[1];
			m_list.push_back(d);
		}
		if (tokens.size() >= 3 && tokens[2] == "true" && !m_list.empty())
			m_list.back().state = true;
		if (tokens.size() >= 5 && !m_list.empty())
		{
			m_list.back().usr = tokens[3];
			m_list.back().pwd = tokens[4];
		}
	}
	UpdateList();
}

void NBDBListDialog::SaveList()
{
	if (VVDQtScriptedRun() && qEnvironmentVariable("VVD_QT_NBLAST_DBLIST_SAVE").isEmpty())
		return;   // TestRun.h (a test names its own file)
	QString listpath = ListPath(true);
	const QString test = qEnvironmentVariable("VVD_QT_NBLAST_DBLIST_SAVE");
	if (!test.isEmpty()) listpath = test;
	QFile f(listpath);
	if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
	QByteArray out;
	for (size_t i = 0; i < m_list.size(); i++)
	{
		const auto& d = m_list[i];
		out += (d.name + "\t" + d.path + "\t" + (d.state ? "true" : "false") + "\t" + d.usr + "\t" + d.pwd).toUtf8();
		if (i + 1 < m_list.size()) out += "\n";   // wx: no newline after the last line
	}
	f.write(out);
}

// ---------------------------------------------------------------------------
// wx wxNBLASTSettingDialog
// ---------------------------------------------------------------------------

NBSettingDialog::NBSettingDialog(NBLASTPluginQt* plugin, QWidget* parent, const QString& title)
	: QDialog(parent), m_plugin(plugin)
{
	setObjectName("NBSettingDialog");
	setWindowTitle(title);
	resize(450, 200);
	QString rpath, tmpdir, rnum;
	if (plugin)
	{
		rpath = plugin->GetRPath();
		tmpdir = plugin->GetOutDir();
		rnum = plugin->GetResultNum();
	}
	auto* l = new QVBoxLayout(this);
	auto* form = new QFormLayout();
	form->addRow(tr("Rscript:"), MakePicker(this, m_RPickCtrl, rpath, tr("Set a path to Rscript"), QString(), false));
	form->addRow(tr("Temporary directory:"), MakePicker(this, m_tmpdirPickCtrl, tmpdir, tr("Choose a temp directory"), QString(), true));
	m_RPickCtrl->setObjectName("NBRscriptPath");
	m_tmpdirPickCtrl->setObjectName("NBTempDir");
	l->addLayout(form);
	auto* row = new QHBoxLayout();
	row->addStretch(1);
	row->addWidget(new QLabel(tr("Show Top"), this));
	m_rnumTextCtrl = new QLineEdit(rnum, this);
	m_rnumTextCtrl->setObjectName("NBResultNum");
	m_rnumTextCtrl->setAlignment(Qt::AlignRight);
	m_rnumTextCtrl->setValidator(new QIntValidator(1, 1000000000, m_rnumTextCtrl));   // wxIntegerValidator min 1
	m_rnumTextCtrl->setFixedWidth(50);
	row->addWidget(m_rnumTextCtrl);
	row->addWidget(new QLabel(tr("Results"), this));
	row->addStretch(1);
	l->addLayout(row);
	auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(bb, &QDialogButtonBox::accepted, this, [this]() { SaveSettings(); accept(); });   // wx OnOk
	connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
	l->addWidget(bb);
}

void NBSettingDialog::SaveSettings()
{
	if (!m_plugin) return;
	m_plugin->SetRPath(m_RPickCtrl->text());
	m_plugin->SetOutDir(m_tmpdirPickCtrl->text());
	m_plugin->SetResultNum(m_rnumTextCtrl->text());
}

// ---------------------------------------------------------------------------
// wx wxImagePanel
// ---------------------------------------------------------------------------

NBImagePanel::NBImagePanel(QWidget* parent)
	: QWidget(parent)
{
	setMinimumSize(100, 50);
	setAutoFillBackground(true);
}

void NBImagePanel::SetImage(const QString& file)
{
	m_orgimage = QImage();
	m_image = QImage();
	m_olimage = QImage();
	m_bgimage = QImage();
	if (!QFileInfo::exists(file)) { update(); return; }
	QImage img(file);
	if (!img.isNull())
	{
		m_orgimage = img.convertToFormat(QImage::Format_RGB888);
		m_image = m_orgimage.copy();
	}
	update();
}

void NBImagePanel::SetOverlayImage(const QString& file)
{
	if (!QFileInfo::exists(file) || m_orgimage.isNull()) return;
	QImage img(file);
	if (img.isNull()) return;
	m_olimage = img.convertToFormat(QImage::Format_RGB888);
}

void NBImagePanel::SetBackgroundImage(const QString& file)
{
	if (!QFileInfo::exists(file) || m_orgimage.isNull()) return;
	QImage img(file);
	if (img.isNull()) return;
	m_bgimage = img.convertToFormat(QImage::Format_RGB888);
}

void NBImagePanel::ResetImage()
{
	if (m_orgimage.isNull()) return;
	m_image = m_orgimage.copy();
	update();
}

// wx wxImagePanel::UpdateImage: the background where the image is black,
// the query (overlay) in red where its red channel >= 64, darkened where > 0
void NBImagePanel::UpdateImage(bool ov_show)
{
	if (m_orgimage.isNull()) return;
	if (m_olimage.isNull()) return;
	const int w = m_orgimage.width();
	const int h = m_orgimage.height();
	if (!m_olimage.isNull() && m_olimage.size() != m_orgimage.size())
		m_olimage = m_olimage.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB888);
	if (!m_bgimage.isNull() && m_bgimage.size() != m_orgimage.size())
		m_bgimage = m_bgimage.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB888);
	m_image = m_orgimage.copy();
	if (!m_bgimage.isNull())
	{
		for (int y = 0; y < h; y++)
		{
			uchar* im = m_image.scanLine(y);
			const uchar* bg = m_bgimage.constScanLine(y);
			for (int x = 0; x < w; x++)
				if (im[x * 3] == 0 && im[x * 3 + 1] == 0 && im[x * 3 + 2] == 0)
				{
					im[x * 3] = bg[x * 3];
					im[x * 3 + 1] = bg[x * 3 + 1];
					im[x * 3 + 2] = bg[x * 3 + 2];
				}
		}
	}
	if (!m_olimage.isNull() && ov_show)
	{
		const double alpha = 0.6;
		const double alpha2 = 0.8;
		// (wx calls m_olimage.Blur(1) and discards the result)
		for (int y = 0; y < h; y++)
		{
			uchar* im = m_image.scanLine(y);
			const uchar* ol = m_olimage.constScanLine(y);
			for (int x = 0; x < w; x++)
			{
				if (ol[x * 3] >= 64)
				{
					im[x * 3] = (uchar)(alpha * 255.0 + (1.0 - alpha) * (double)im[x * 3]);
					im[x * 3 + 1] = (uchar)(alpha * 0.0 + (1.0 - alpha) * (double)im[x * 3 + 1]);
					im[x * 3 + 2] = (uchar)(alpha * 0.0 + (1.0 - alpha) * (double)im[x * 3 + 2]);
				}
				else if (ol[x * 3] > 0)
				{
					im[x * 3] = (uchar)(alpha2 * 0.0 + (1.0 - alpha2) * (double)im[x * 3]);
					im[x * 3 + 1] = (uchar)(alpha2 * 0.0 + (1.0 - alpha2) * (double)im[x * 3 + 1]);
					im[x * 3 + 2] = (uchar)(alpha2 * 0.0 + (1.0 - alpha2) * (double)im[x * 3 + 2]);
				}
			}
		}
	}
	update();
}

QSize NBImagePanel::CalcImageSizeKeepAspectRatio(int w, int h) const
{
	if (m_orgimage.isNull()) return QSize(-1, -1);
	const int orgw = m_orgimage.width();
	const int orgh = m_orgimage.height();
	double nw = w;
	double nh = (double)w * ((double)orgh / (double)orgw);
	if (nh <= h) return QSize((int)nw, (int)nh);
	nw = (double)h * ((double)orgw / (double)orgh);
	nh = h;
	return QSize((int)nw, (int)nh);
}

double NBImagePanel::GetAspectRatio() const
{
	if (m_orgimage.isNull()) return 0.0;
	const int orgw = m_orgimage.width();
	const int orgh = m_orgimage.height();
	return orgh != 0 ? orgw / orgh : 0.0;   // integer division, as wx
}

void NBImagePanel::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	p.fillRect(rect(), palette().window());
	if (m_image.isNull()) return;
	const QSize ns = CalcImageSizeKeepAspectRatio(width(), height());
	if (ns.width() <= 0 || ns.height() <= 0) return;
	const int posx = width() > ns.width() ? (width() - ns.width()) / 2 : 0;
	const int posy = height() > ns.height() ? (height() - ns.height()) / 2 : 0;
	p.drawImage(QPoint(posx, posy), m_image.scaled(ns, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
}

// ---------------------------------------------------------------------------
// wx NBLASTListCtrl
// ---------------------------------------------------------------------------

NBResultList::NBResultList(QWidget* parent)
	: QWidget(parent)
{
	auto* l = new QVBoxLayout(this);
	l->setContentsMargins(0, 0, 0, 0);
	m_tree = new QTreeWidget(this);
	m_tree->setObjectName("NBResultList");
	m_tree->setColumnCount(7);
	m_tree->setHeaderLabels({ QString(), QString(), tr("Name"), tr("Database"), tr("Skeleton"), tr("Volume Data"), tr("Score") });
	m_tree->headerItem()->setTextAlignment(6, Qt::AlignRight | Qt::AlignVCenter);
	m_tree->setRootIsDecorated(false);
	m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
	m_tree->setUniformRowHeights(true);
	m_tree->header()->setStretchLastSection(false);
	// wx: columns 0 and 1 zero width, not resizable (OnColBeginDrag veto)
	m_tree->setColumnHidden(0, true);
	m_tree->setColumnHidden(1, true);
	m_tree->setColumnWidth(2, 110);
	m_tree->setColumnWidth(3, 100);
	m_tree->setColumnWidth(4, 150);
	m_tree->setColumnWidth(5, 150);
	m_tree->setColumnWidth(6, 110);
	m_tree->installEventFilter(this);
	l->addWidget(m_tree);
	connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &NBResultList::OnSelect);
	// wx OnLeftDClick: the Skeleton / Volume Data cell of the selected row
	connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* it, int col) {
		ActivateCell(m_tree->indexOfTopLevelItem(it), col);
	});
}

int NBResultList::GetItemCount() const { return m_tree->topLevelItemCount(); }

QString NBResultList::GetText(int item, int col) const
{
	QTreeWidgetItem* it = m_tree->topLevelItem(item);
	return it ? it->text(col) : QString();
}

int NBResultList::GetImageId(int item, int col) const
{
	QTreeWidgetItem* it = m_tree->topLevelItem(item);
	if (!it) return -1;
	bool ok = false;
	const int id = it->data(col, Qt::UserRole).toInt(&ok);
	return ok ? id : -1;
}

int NBResultList::GetSelected() const
{
	QTreeWidgetItem* it = m_tree->currentItem();
	if (!it || !it->isSelected()) return -1;
	return m_tree->indexOfTopLevelItem(it);
}

void NBResultList::Select(int item)
{
	if (QTreeWidgetItem* it = m_tree->topLevelItem(item))
	{
		m_tree->setCurrentItem(it);
		it->setSelected(true);
	}
}

void NBResultList::Append(const QString& name, const QString& dbname, const QString& score, int mipid, int swcid, int dbid, int index)
{
	const QString dbidstr = QString::number(dbid);
	const int itemid = index >= 0 ? std::min(index, GetItemCount()) : GetItemCount();
	auto* it = new QTreeWidgetItem();
	it->setText(0, name);
	it->setText(1, dbidstr);
	it->setText(2, name);
	it->setText(3, dbname);
	it->setText(6, score);
	it->setTextAlignment(6, Qt::AlignRight | Qt::AlignVCenter);
	it->setData(4, Qt::UserRole, swcid);
	it->setData(5, Qt::UserRole, mipid);
	if (swcid >= 0 && swcid < (int)m_images.size())
		it->setData(4, Qt::DecorationRole, QPixmap::fromImage(m_images[swcid]));
	if (mipid >= 0 && mipid < (int)m_images.size())
		it->setData(5, Qt::DecorationRole, QPixmap::fromImage(m_images[mipid]));
	m_tree->insertTopLevelItem(itemid, it);
}

void NBResultList::LoadResults(const QString& csvfilepath)
{
	QSignalBlocker block(m_tree);   // wx SetEvtHandlerEnabled(false)
	m_tree->clear();
	m_dbdirs.clear();
	m_dbpaths.clear();
	m_dbnames.clear();
	m_listdata.clear();
	m_images.clear();
	m_rfpath = csvfilepath;

	QFile f(csvfilepath);
	QStringList lines;
	if (f.open(QIODevice::ReadOnly | QIODevice::Text))
	{
		QTextStream ts(&f);
		ts.setEncoding(QStringConverter::Utf8);
		while (!ts.atEnd()) lines << ts.readLine();
	}
	int li = 0;
	if (li < lines.size())
		m_dbnames = Tokenize(lines[li++], ",");
	if (li < lines.size())
	{
		m_dbpaths = Tokenize(lines[li++], ",");
		for (const QString& tk : m_dbpaths)
			m_dbdirs << BeforeLast(QDir::fromNativeSeparators(tk), kSep);
	}
	for (; li < lines.size(); li++)
	{
		const QStringList con = Tokenize(lines[li], ",");
		if (con.size() >= 2)
		{
			NBLASTListItemData data;
			data.name = con[0];
			int tp = con[1].trimmed().toInt();
			if (tp < 0 || tp >= m_dbdirs.size()) tp = -1;
			data.dbid = tp;
			data.score = con.size() >= 3 ? con[2] : QString();
			m_listdata.push_back(data);
		}
	}

	bool show_image = false;
	int w = 0, h = 0;
	if (!m_dbdirs.isEmpty())
	{
		for (const char* sub : { "MIP_thumb", "swc_thumb" })
		{
			QDir d(m_dbdirs[0] + kSep + sub);
			const QStringList pngs = d.exists() ? d.entryList(QStringList() << "*.png", QDir::Files) : QStringList();
			if (!pngs.isEmpty())
			{
				QImage img(d.filePath(pngs.first()));
				if (!img.isNull())
				{
					w = img.width();
					h = img.height();
#ifdef __APPLE__
					m_tree->setColumnWidth(4, w + 8);
					m_tree->setColumnWidth(5, w + 8);
#else
					m_tree->setColumnWidth(4, w + 2);
					m_tree->setColumnWidth(5, w + 2);
#endif
					show_image = true;
					break;
				}
			}
		}
	}

	if (show_image)
	{
		m_thumb = QSize(w, h);
		m_tree->setIconSize(m_thumb);
		// wx wxImageList(w, h): index 0 a black dummy
		QImage dummy(w, h, QImage::Format_RGB888);
		dummy.fill(Qt::black);
		m_images.push_back(dummy);
		int imgcount = 0;
		for (auto& d : m_listdata)
		{
			int mipid = 0, swcid = 0;
			if (d.dbid >= 0 && d.dbid < m_dbdirs.size())
			{
				const QString thumb = m_dbdirs[d.dbid] + "/MIP_thumb/" + d.name + ".png";
				const QString prev = m_dbdirs[d.dbid] + "/swc_thumb/" + d.name + ".png";
				if (QFileInfo::exists(thumb))
				{
					QImage img(thumb);
					if (!img.isNull())
					{
						imgcount++;
						m_images.push_back(img.size() == m_thumb ? img : img.scaled(m_thumb));
						mipid = imgcount;
					}
				}
				if (QFileInfo::exists(prev))
				{
					QImage img(prev);
					if (!img.isNull())
					{
						imgcount++;
						m_images.push_back(img.size() == m_thumb ? img : img.scaled(m_thumb));
						swcid = imgcount;
					}
				}
			}
			QString dbname;
			if (d.dbid >= 0 && d.dbid < m_dbnames.size()) dbname = m_dbnames[d.dbid];
			else if (d.dbid >= 0 && d.dbid < m_dbpaths.size()) dbname = QFileInfo(m_dbpaths[d.dbid]).completeBaseName();
			d.swcid = swcid;
			d.mipid = mipid;
			d.dbname = dbname;
			Append(d.name, d.dbname, d.score, d.mipid, d.swcid, d.dbid);
		}
	}
	else
	{
		for (auto& d : m_listdata)
		{
			QString dbname;
			if (d.dbid >= 0 && d.dbid < m_dbnames.size()) dbname = m_dbnames[d.dbid];
			else if (d.dbid >= 0 && d.dbid < m_dbpaths.size()) dbname = QFileInfo(m_dbpaths[d.dbid]).completeBaseName();
			d.swcid = -1;
			d.mipid = -1;
			d.dbname = dbname;
			Append(d.name, d.dbname, d.score, d.mipid, d.swcid, d.dbid);
		}
	}
	block.unblock();
	// wx: the first item selected
	if (GetItemCount() > 0) Select(0);
}

void NBResultList::SaveResults(const QString& txtpath, bool export_swc, bool export_swcprev, bool export_mip,
	bool export_vol, bool pfx_score, bool pfx_db, bool zip)
{
	QFile f(txtpath);
	if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
	{
		QTextStream tos(&f);
		tos.setEncoding(QStringConverter::Utf8);
		if (!m_dbnames.isEmpty()) tos << m_dbnames.join(",") << "\n";
		if (!m_dbpaths.isEmpty()) tos << m_dbpaths.join(",") << "\n";
		for (int item = 0; item < GetItemCount(); item++)
			tos << GetText(item, 2) << "," << GetText(item, 1) << "," << GetText(item, 6) << "\n";
	}
	f.close();

	const QString outdir = BeforeLast(QDir::fromNativeSeparators(txtpath), kSep);
	const QString prjimg = BeforeLast(m_rfpath, '.') + ".png";
	const QString new_prjimg = outdir + kSep + QFileInfo(txtpath).completeBaseName() + ".png";
	if (prjimg != new_prjimg) CopyOver(prjimg, new_prjimg);

	if ((export_swc || export_swcprev || export_mip || export_vol) && !zip && GetItemCount() > 0)
	{
		QProgressDialog prg(tr("NBLAST Plugin: Exporting search results..."), QString(), 0, GetItemCount(), this);
		prg.setWindowTitle(tr("NBLAST Plugin: Exporting search results..."));
		prg.setWindowModality(Qt::ApplicationModal);
		prg.setMinimumDuration(300);
		for (int item = 0; item < GetItemCount(); item++)
		{
			const int dbid = GetText(item, 1).toInt();
			if (dbid >= 0 && dbid < m_dbdirs.size())
			{
				const QString name = GetText(item, 2);
				QString prefix;
				if (pfx_score)
				{
					bool ok = false;
					const double sc = GetText(item, 6).trimmed().toDouble(&ok);
					if (ok)
					{
						const int pfsc = (int)(floor(sc * 100000.0 + 0.5));
						prefix += QString::number(pfsc) + " ";
					}
				}
				if (pfx_db && dbid < m_dbnames.size())
					prefix += m_dbnames[dbid] + " ";
				if (export_swcprev)
					CopyOver(m_dbdirs[dbid] + "/swc_prev/" + name + ".png", outdir + kSep + prefix + name + "_swc.png");
				if (export_mip)
					CopyOver(m_dbdirs[dbid] + "/MIP/" + name + ".png", outdir + kSep + prefix + name + "_mip.png");
				if (export_swc)
					CopyOver(m_dbdirs[dbid] + "/swc/" + name + ".swc", outdir + kSep + prefix + name + ".swc");
				if (export_vol)
					CopyOver(m_dbdirs[dbid] + "/volume/" + name + ".nrrd", outdir + kSep + prefix + name + ".nrrd");
			}
			prg.setValue(item + 1);
		}
	}
}

void NBResultList::DeleteSelection()
{
	const int item = GetSelected();
	if (item < 0) return;
	int sel = item;
	if (item + 1 >= GetItemCount())
		sel = item - 1;   // wx GetNextItem(item, wxLIST_NEXT_ABOVE)
	NBLASTListItemData hdata;
	hdata.dbid = GetText(item, 1).toInt();
	hdata.name = GetText(item, 2);
	hdata.dbname = GetText(item, 3);
	hdata.swcid = GetImageId(item, 4);
	hdata.mipid = GetImageId(item, 5);
	hdata.score = GetText(item, 6);
	hdata.itemid = item;
	AddHistory(hdata);
	delete m_tree->takeTopLevelItem(item);
	if (sel >= 0)
	{
		// wx SetItemState(sel, ...): the row now at sel (the next one)
		const int s = std::min(item, GetItemCount() - 1);
		if (s >= 0) Select(s);
	}
}

void NBResultList::DeleteAll()
{
	m_tree->clear();
	m_history.clear();
	m_history_pos = 0;
}

void NBResultList::AddHistory(const NBLASTListItemData& data)
{
	if (m_history_pos < (int)m_history.size())
	{
		if (m_history_pos > 0)
			m_history = std::vector<NBLASTListItemData>(m_history.begin(), m_history.begin() + m_history_pos);
		else
		{
			m_history.clear();
			m_history_pos = 0;
		}
	}
	if (m_history_pos >= 300)
	{
		m_history.erase(m_history.begin());
		m_history.push_back(data);
	}
	else
	{
		m_history.push_back(data);
		m_history_pos++;
	}
}

void NBResultList::Undo()
{
	if (m_history_pos <= 0) return;
	m_history_pos--;
	const NBLASTListItemData comdata = m_history[m_history_pos];
	Append(comdata.name, comdata.dbname, comdata.score, comdata.mipid, comdata.swcid, comdata.dbid, comdata.itemid);
}

void NBResultList::Redo()
{
	if (m_history_pos >= (int)m_history.size()) return;
	const NBLASTListItemData comdata = m_history[m_history_pos];
	if (comdata.itemid >= 0 && comdata.itemid < GetItemCount())
		delete m_tree->takeTopLevelItem(comdata.itemid);
	m_history_pos++;
}

void NBResultList::OnSelect()
{
	const int item = GetSelected();
	if (item < 0) return;
	const int dbid = GetText(item, 1).toInt();
	if (dbid >= 0 && dbid < m_dbdirs.size())
	{
		const QString name = GetText(item, 2);
		Q_EMIT setImages(m_dbdirs[dbid] + "/swc_prev/" + name + ".png", m_dbdirs[dbid] + "/MIP/" + name + ".png");
	}
}

void NBResultList::ActivateCell(int item, int col)
{
	if (item < 0) return;
	const int dbid = GetText(item, 1).toInt();
	if (dbid < 0 || dbid >= m_dbdirs.size()) return;
	const QString name = GetText(item, 2);
	if (col == 4)
	{
		const QString swcpath = m_dbdirs[dbid] + "/swc/" + name + ".swc";
		if (QFileInfo::exists(swcpath)) Q_EMIT openFile(swcpath);
	}
	else if (col == 5)
	{
		const QString volpath = m_dbdirs[dbid] + "/volume/" + name + ".nrrd";
		if (QFileInfo::exists(volpath)) Q_EMIT openFile(volpath);
	}
}

bool NBResultList::eventFilter(QObject* o, QEvent* e)
{
	if (o == m_tree && e->type() == QEvent::KeyPress)
	{
		auto* ke = static_cast<QKeyEvent*>(e);
		const Qt::KeyboardModifiers m = ke->modifiers();
		// wx OnKeyDown (WXK_CONTROL is the Command key on macOS: Qt's Control)
		const bool ctrl = m & Qt::ControlModifier, shift = m & Qt::ShiftModifier, alt = m & Qt::AltModifier;
		if (ke->key() == Qt::Key_Delete || ke->key() == Qt::Key_Backspace)
		{
			DeleteSelection();
			return true;
		}
		if (ke->key() == Qt::Key_Z && ctrl && !shift && !alt) { Undo(); return true; }
		if (ke->key() == Qt::Key_Y && ctrl && !shift && !alt) { Redo(); return true; }
		if (ke->key() == Qt::Key_Z && ctrl && shift && !alt) { Redo(); return true; }
	}
	return QWidget::eventFilter(o, e);
}

// ---------------------------------------------------------------------------
// wx NBLASTGuiPluginWindow
// ---------------------------------------------------------------------------

NBLASTPanel::NBLASTPanel(NBLASTPluginQt* plugin, QWidget* parent)
	: QWidget(parent), m_plugin(plugin)
{
	setObjectName("NBLASTPanel");
	QString scmtd = plugin ? plugin->GetScoringMethod() : QString();

	auto* top = new QHBoxLayout(this);
	top->setContentsMargins(0, 0, 0, 0);
	m_splitterWindow = new QSplitter(Qt::Horizontal, this);
	top->addWidget(m_splitterWindow);
	auto* nbpanel = new QFrame(m_splitterWindow);
	nbpanel->setFrameShape(QFrame::Box);
	auto* imgpanel = new QFrame(m_splitterWindow);
	imgpanel->setFrameShape(QFrame::Box);
	m_nbpanel = nbpanel;

	auto* v = new QVBoxLayout(nbpanel);
	v->setContentsMargins(0, 0, 0, 0);
	m_tb = new QToolBar(nbpanel);
	m_tb->setObjectName("NBToolBar");
	m_tb->setToolButtonStyle(Qt::ToolButtonTextOnly);
	QAction* a_save = m_tb->addAction(tr("Save"), this, &NBLASTPanel::OnSave);
	a_save->setToolTip(tr("Save search results"));
	m_tb->addSeparator();
	QAction* a_imp = m_tb->addAction(tr("Import"), this, &NBLASTPanel::OnImportResults);
	a_imp->setToolTip(tr("Import search results"));
	m_tb->addSeparator();
	QAction* a_db = m_tb->addAction(tr("Database"), this, &NBLASTPanel::OnEditDB);
	a_db->setToolTip(tr("Edit NBLAST databases"));
	m_tb->addSeparator();
	QAction* a_set = m_tb->addAction(tr("Setting"), this, &NBLASTPanel::OnSettingButton);
	a_set->setToolTip(tr("Setting"));
	v->addWidget(m_tb);
	auto* line = new QFrame(nbpanel);
	line->setFrameShape(QFrame::HLine);
	v->addWidget(line);

	NBDBListDialog dbdlg(this, tr("Edit NBLAST Database"));
	m_nlib_list = dbdlg.getList();

	auto* sc_row = new QHBoxLayout();
	sc_row->addStretch(1);
	sc_row->addWidget(new QLabel(tr("Scoring Method:"), nbpanel));
	sc_row->addSpacing(20);
	m_sc_forward = new QRadioButton(tr("Forward"), nbpanel);
	m_sc_forward->setObjectName("NBForward");
	m_sc_mean = new QRadioButton(tr("Mean"), nbpanel);
	m_sc_mean->setObjectName("NBMean");
	auto* grp = new QButtonGroup(this);
	grp->addButton(m_sc_forward);
	grp->addButton(m_sc_mean);
	if (scmtd == "mean") m_sc_mean->setChecked(true);
	else m_sc_forward->setChecked(true);
	sc_row->addWidget(m_sc_forward);
	sc_row->addWidget(m_sc_mean);
	sc_row->addStretch(1);
	v->addLayout(sc_row);
	// wx OnScoringMethodCheck
	connect(m_sc_forward, &QRadioButton::toggled, this, [this](bool on) { if (on && m_plugin) m_plugin->SetScoringMethod("forward"); });
	connect(m_sc_mean, &QRadioButton::toggled, this, [this](bool on) { if (on && m_plugin) m_plugin->SetScoringMethod("mean"); });

	m_nlib_box = new QGroupBox(tr("Target Neurons"), nbpanel);
	m_nlib_grid = new QGridLayout(m_nlib_box);
	m_nlib_grid->setHorizontalSpacing(10);
	m_nlib_grid->setVerticalSpacing(20);
	auto* box_row = new QHBoxLayout();
	box_row->addStretch(1);
	box_row->addWidget(m_nlib_box);
	box_row->addStretch(1);
	v->addLayout(box_row);
	BuildDatabaseChecks(m_nlib_list);

	m_CommandButton = new QPushButton(tr("Run NBLAST"), nbpanel);
	m_CommandButton->setObjectName("NBRunButton");
	v->addWidget(m_CommandButton, 0, Qt::AlignHCenter);
	connect(m_CommandButton, &QPushButton::clicked, this, &NBLASTPanel::OnRunNBLAST);

	m_results = new NBResultList(nbpanel);
	v->addWidget(m_results, 1);
	connect(m_results, &NBResultList::openFile, this, [this](const QString& p) { if (m_plugin) m_plugin->LoadFiles(p); });
	connect(m_results, &NBResultList::setImages, this, &NBLASTPanel::OnSetImages);

	auto* v2 = new QVBoxLayout(imgpanel);
	m_overlayChk = new QCheckBox(tr("Overlay search query"), imgpanel);
	m_overlayChk->setObjectName("NBOverlayCheck");
	m_overlayChk->setChecked(true);
	connect(m_overlayChk, &QCheckBox::toggled, this, &NBLASTPanel::OnOverlayCheck);
	auto* chk_row = new QHBoxLayout();
	chk_row->addSpacing(20);
	chk_row->addWidget(m_overlayChk);
	chk_row->addStretch(1);
	v2->addLayout(chk_row);
	m_swcImagePanel = new NBImagePanel(imgpanel);
	m_mipImagePanel = new NBImagePanel(imgpanel);
	v2->addWidget(m_swcImagePanel, 1);
	v2->addWidget(m_mipImagePanel, 1);

	// wx: SetMinSize(640, 750) / (510, 750); smaller here, the pane is a dock
	nbpanel->setMinimumSize(640, 450);
	imgpanel->setMinimumSize(300, 450);
	m_splitterWindow->addWidget(nbpanel);
	m_splitterWindow->addWidget(imgpanel);

	// wx: plugin->GetEventHandler()->Bind(wxEVT_GUI_PLUGIN_INTEROP, OnInteropMessageReceived)
	if (m_plugin && m_plugin->GetHost() && m_plugin->GetHost()->GetPluginManager())
		connect(m_plugin->GetHost()->GetPluginManager(), &QtPluginManager::interop, this, &NBLASTPanel::OnInterop);
}

NBLASTPanel::~NBLASTPanel()
{
	if (m_plugin)
		m_plugin->SaveConfigFile();
	// wx: the database list with the check states
	NBDBListDialog dbdlg(nullptr, tr("Edit NBLAST Database"));
	for (size_t i = 0; i < m_nlib_chks.size(); i++)
		dbdlg.setState(i, m_nlib_chks[i]->isChecked());
	dbdlg.SaveList();
}

void NBLASTPanel::BuildDatabaseChecks(const std::vector<NBLASTDBListItemData>& list)
{
	for (QCheckBox* c : m_nlib_chks) delete c;
	m_nlib_chks.clear();
	const int cols = 5;
	for (size_t i = 0; i < list.size(); i++)
	{
		auto* c = new QCheckBox(list[i].name, m_nlib_box);
		c->setObjectName(QString("NBDB_%1").arg(i));
		c->setChecked(list[i].state);
		m_nlib_grid->addWidget(c, (int)i / cols, (int)i % cols, Qt::AlignCenter);
		m_nlib_chks.push_back(c);
	}
}

void NBLASTPanel::ReloadDatabaseChecks()
{
	NBDBListDialog dbdlg(this, tr("Edit NBLAST Database"));
	m_nlib_list = dbdlg.getList();
	BuildDatabaseChecks(m_nlib_list);
}

void NBLASTPanel::EnableControls(bool enable)
{
	if (m_results) m_results->setEnabled(enable);
	if (m_CommandButton) m_CommandButton->setEnabled(enable);
}

void NBLASTPanel::OnSetImages(const QString& imgpath1, const QString& imgpath2)
{
	// wx doAction(NB_SET_IMAGE)
	if (!m_results || !m_swcImagePanel || !m_mipImagePanel) return;
	const QString prjimg = BeforeLast(m_results->GetListFilePath(), '.') + ".png";
	double aspect = 0.0;
	{
		const QString bkimgpath = BeforeLast(imgpath1, kSep) + "/bg.png";
		m_swcImagePanel->SetImage(imgpath1);
		m_swcImagePanel->SetOverlayImage(prjimg);
		m_swcImagePanel->SetBackgroundImage(bkimgpath);
		m_swcImagePanel->UpdateImage(m_overlayChk->isChecked());
		aspect = m_swcImagePanel->GetAspectRatio();
	}
	{
		const QString bkimgpath = BeforeLast(imgpath2, kSep) + "/bg.png";
		m_mipImagePanel->SetImage(imgpath2);
		const double a2 = m_mipImagePanel->GetAspectRatio();
		if (std::fabs(aspect - a2) < 0.00001)
			m_mipImagePanel->SetOverlayImage(prjimg);
		m_mipImagePanel->SetBackgroundImage(bkimgpath);
		m_mipImagePanel->UpdateImage(m_overlayChk->isChecked());
	}
}

void NBLASTPanel::OnOverlayCheck()
{
	if (!m_swcImagePanel || !m_mipImagePanel) return;
	m_swcImagePanel->UpdateImage(m_overlayChk->isChecked());
	const double a1 = m_swcImagePanel->GetAspectRatio();
	const double a2 = m_mipImagePanel->GetAspectRatio();
	m_mipImagePanel->UpdateImage(m_overlayChk->isChecked() && std::fabs(a1 - a2) < 0.00001);
}

void NBLASTPanel::OnRunNBLAST()
{
	QStringList nlibs, nlibnames;
	for (size_t i = 0; i < m_nlib_list.size() && i < m_nlib_chks.size(); i++)
		if (m_nlib_chks[i]->isChecked())
		{
			nlibs << m_nlib_list[i].path;
			nlibnames << m_nlib_list[i].name;
		}
	const QString nlibpath = nlibs.join(",");
	const QString nlibname = nlibnames.join(",");
	if (!m_plugin) return;
	QWidget* w = window();
	const QString rpath = m_plugin->GetRPath();
	const QString outdir = m_plugin->GetOutDir();
	const QString ofname = "vvd_nblast_tmp_result";
	const QString rnum = m_plugin->GetResultNum();
	const QString scmtd = m_plugin->GetScoringMethod();
	if (!QFileInfo::exists(rpath)) { Report(w, "Could not find Rscript binary"); return; }
	if (nlibs.isEmpty()) { Report(w, "Choose target neurons"); return; }
	for (const QString& nl : nlibs)
		if (!QFileInfo::exists(nl)) { Report(w, QString("Could not find a NBLAST database (%1)").arg(nl)); return; }
	if (outdir.isEmpty()) { Report(w, "Set a temp directory"); return; }
	bool ok = false;
	rnum.toLong(&ok);
	if (!ok) { Report(w, "Invalid number"); return; }

	vvd::PluginHost* host = m_plugin->GetHost();
	if (!host) return;
	const int type = host->GetCurSelType();
	VolumeData* vd = host->GetCurSelVol();
	MeshData* md = host->GetCurSelMesh();
	if (vd && type == 2)
	{
		if (!vd->GetName().ToQt().contains("skeleton"))
		{
			if (m_plugin->skeletonizeMask())
			{
				m_plugin->SetRPath(rpath);
				m_plugin->SetNlibPath(nlibpath);
				m_plugin->SetOutDir(outdir);
				m_plugin->SetFileName(ofname);
				m_plugin->SetResultNum(rnum);
				m_plugin->SetDatabaseNames(nlibname);
				m_plugin->SetScoringMethod(scmtd);
				m_waitingforFiji = true;
			}
		}
		else if (!m_waitingforFiji)
		{
			m_plugin->runNBLAST(rpath, nlibpath, outdir, ofname, rnum, nlibname, scmtd);
			const QString respath = outdir + kSep + ofname + ".txt";
			if (QFileInfo::exists(respath) && m_results)
				m_results->LoadResults(respath);
		}
	}
	if (md && type == 3)
	{
		m_plugin->runNBLAST(rpath, nlibpath, outdir, ofname, rnum, nlibname, scmtd);
		const QString respath = outdir + kSep + ofname + ".txt";
		if (QFileInfo::exists(respath) && m_results)
			m_results->LoadResults(respath);
	}
}

QWidget* NBLASTPanel::CreateExtraNBLASTControl(QWidget* parent)
{
	auto* panel = new QGroupBox(tr("Additional Options"), parent);
	auto* sizer = new QVBoxLayout(panel);
	auto* group1 = new QHBoxLayout();
	group1->addWidget(new QLabel(tr("Export:"), panel));
	group1->addSpacing(20);
	struct C { const char* text; const char* name; bool (*get)(); void (*set)(bool); };
	const C g1[] = {
		{ "Skeletones", "export_swc", &NBLASTPluginQt::GetExportSWCs, &NBLASTPluginQt::SetExportSWCs },
		{ "MIPs", "export_mip", &NBLASTPluginQt::GetExportMIPs, &NBLASTPluginQt::SetExportMIPs },
		{ "Skeletone Previews", "export_swcprev", &NBLASTPluginQt::GetExportSWCPrevImgs, &NBLASTPluginQt::SetExportSWCPrevImgs },
		{ "Volumes", "export_vol", &NBLASTPluginQt::GetExportVolumes, &NBLASTPluginQt::SetExportVolumes },
	};
	for (const C& c : g1)
	{
		auto* ch = new QCheckBox(tr(c.text), panel);
		ch->setObjectName(c.name);
		ch->setChecked(c.get());
		auto set = c.set;
		connect(ch, &QCheckBox::toggled, panel, [set](bool on) { set(on); });
		group1->addWidget(ch);
		group1->addSpacing(20);
	}
	group1->addStretch(1);
	auto* group2 = new QHBoxLayout();
	group2->addWidget(new QLabel(tr("File Name Prefix:"), panel));
	group2->addSpacing(20);
	const C g2[] = {
		{ "Score", "prefix_score", &NBLASTPluginQt::GetPrefixScore, &NBLASTPluginQt::SetPrefixScore },
		{ "Database", "prefix_database", &NBLASTPluginQt::GetPrefixDatabase, &NBLASTPluginQt::SetPrefixDatabase },
	};
	for (const C& c : g2)
	{
		auto* ch = new QCheckBox(tr(c.text), panel);
		ch->setObjectName(c.name);
		ch->setChecked(c.get());
		auto set = c.set;
		connect(ch, &QCheckBox::toggled, panel, [set](bool on) { set(on); });
		group2->addWidget(ch);
		group2->addSpacing(20);
	}
	group2->addStretch(1);
	sizer->addLayout(group1);
	sizer->addSpacing(15);
	sizer->addLayout(group2);
	return panel;
}

void NBLASTPanel::OnSave()
{
	if (!m_results || !m_plugin) return;
	QFileDialog dlg(this, tr("Save Search Results"), QString(), "*.txt");
	dlg.setOption(QFileDialog::DontUseNativeDialog, true);
	dlg.setAcceptMode(QFileDialog::AcceptSave);
	dlg.setFileMode(QFileDialog::AnyFile);
	AddOptions(dlg, CreateExtraNBLASTControl(&dlg));
	if (FileDialogs::Exec(dlg, "NBLASTSAVE") != QDialog::Accepted) return;
	const QStringList files = dlg.selectedFiles();
	if (files.isEmpty()) return;
	m_results->SaveResults(files.first(), NBLASTPluginQt::GetExportSWCs(), NBLASTPluginQt::GetExportSWCPrevImgs(),
		NBLASTPluginQt::GetExportMIPs(), NBLASTPluginQt::GetExportVolumes(), NBLASTPluginQt::GetPrefixScore(),
		NBLASTPluginQt::GetPrefixDatabase());
}

void NBLASTPanel::OnEditDB()
{
	auto* dbdlg = new NBDBListDialog(this, tr("Edit NBLAST Database"));
	for (size_t i = 0; i < m_nlib_chks.size(); i++)
		dbdlg->setState(i, m_nlib_chks[i]->isChecked());
	// the dialog's list starts with the panel's check states
	if (dbdlg->exec() == QDialog::Accepted)
	{
		m_nlib_list = dbdlg->getList();
		BuildDatabaseChecks(m_nlib_list);
		m_nbpanel->updateGeometry();
	}
	delete dbdlg;
}

void NBLASTPanel::OnImportResults()
{
	if (!m_results || !m_plugin) return;
	const QString f = PickFile(this, "NBLASTIMPORT", tr("Import Search Results"), QString(), "*.txt");
	if (!f.isEmpty())
		m_results->LoadResults(f);
}

void NBLASTPanel::OnSettingButton()
{
	NBSettingDialog sdlg(m_plugin, this, tr("NBLAST Settings"));
	sdlg.exec();
}

void NBLASTPanel::OnInterop(const QString& message)
{
	// wx OnInteropMessageReceived: "Fiji Interface,finished,NBLAST Skeletonize"
	if (!m_waitingforFiji || !m_plugin) return;
	const QStringList args = Tokenize(message, ",");
	if (args.size() >= 3 && args[0] == "Fiji Interface" && args[1] == "finished" && args[2] == "NBLAST Skeletonize")
	{
		m_waitingforFiji = false;
		m_plugin->runNBLAST();
		const QString respath = m_plugin->GetOutDir() + kSep + m_plugin->GetFileName() + ".txt";
		if (QFileInfo::exists(respath) && m_results)
			m_results->LoadResults(respath);
	}
}
