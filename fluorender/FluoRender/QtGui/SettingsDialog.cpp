#include "SettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleValidator>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QSlider>
#include <QTabWidget>
#include <QVBoxLayout>

#include <cmath>

#include "DataManager.h"
#include "Core/FileUtil.h"
#include "FLIVR/Texture.h"
#include "FLIVR/TextureRenderer.h"
#include "FLIVR/VolumeRenderer.h"
#include "FLIVR/MultiVolumeRenderer.h"
#include "Converters/VolumeMeshConv.h"

#ifndef VERSION_MAJOR_TAG
#define VERSION_MAJOR_TAG "2"
#endif
#ifndef VERSION_MINOR_TAG
#define VERSION_MINOR_TAG "0"
#endif

namespace
{
	const double kPi = 3.14159265358979323846;

	// width for a small number field: wide enough for `sample`, no clipping
	void FitWidth(QLineEdit* e, const char* sample)
	{
		const int w = e->fontMetrics().horizontalAdvance(QString::fromLatin1(sample)) + 16;
		e->setMinimumWidth(w);
		e->setMaximumWidth(w + w / 2);
	}

	QSlider* MakeSlider(int val, int lo, int hi)
	{
		QSlider* s = new QSlider(Qt::Horizontal);
		s->setRange(lo, hi);
		s->setValue(val);
		return s;
	}

	QLabel* MakeText(const QString& s)
	{
		QLabel* l = new QLabel(s);
		l->setTextFormat(Qt::PlainText);
		return l;
	}

	// wx str.ToDouble / ToLong: C locale, false on garbage
	double ToDouble(const QString& s, bool* ok = nullptr)
	{
		return QLocale::c().toDouble(s.trimmed(), ok);
	}

	bool HasGroup(const vvd::IniFile& cfg, const wchar_t* group)
	{
		return !cfg.Keys(group).empty();
	}

	QString FromVVD(const VVDString& s) { return QString::fromStdWString(s); }
	VVDString ToVVD(const QString& s) { return VVDString(s.toStdWString()); }

	class SignalBlock
	{
	public:
		explicit SignalBlock(QObject* o) : m_o(o), m_old(o ? o->blockSignals(true) : false) {}
		~SignalBlock() { if (m_o) m_o->blockSignals(m_old); }
	private:
		QObject* m_o;
		bool m_old;
	};
}

// ---------------------------------------------------------------------------
// pages

QWidget* SettingsDialog::CreateProjectPage()
{
	QWidget* page = new QWidget;

	// Open/Save
	QGroupBox* group1 = new QGroupBox(tr("Open/Save"));
	QVBoxLayout* g1 = new QVBoxLayout(group1);
	m_prj_save_chk = new QCheckBox(tr("Save project when capture viewport or export movie."));
	m_realtime_cmp_chk = new QCheckBox(tr("Compress data in graphics memory when loading."));
	g1->addWidget(m_prj_save_chk);
	g1->addWidget(m_realtime_cmp_chk);

	// Render View Text
	QGroupBox* group2 = new QGroupBox(tr("Render View Text"));
	QVBoxLayout* g2 = new QVBoxLayout(group2);
	QHBoxLayout* s2_1 = new QHBoxLayout;
	m_font_cmb = new QComboBox;
	m_font_cmb->setMinimumContentsLength(14);
	m_font_cmb->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	{
		QDir dir(FontsDir());
		const QStringList files = dir.entryList(QStringList() << "*.ttf", QDir::Files, QDir::Name);
		for (const QString& f : files)
			m_font_cmb->addItem(QFileInfo(f).completeBaseName());
	}
	m_font_size_cmb = new QComboBox;
	for (int font_size = 10; font_size < 31; font_size += 2)
		m_font_size_cmb->addItem(QString::number(font_size));
	m_text_color_cmb = new QComboBox;
	m_text_color_cmb->addItem(tr("BG inverted"));
	m_text_color_cmb->addItem(tr("Background"));
	m_text_color_cmb->addItem(tr("Vol sec color"));
	s2_1->addWidget(MakeText(tr("Font:")));
	s2_1->addWidget(m_font_cmb);
	s2_1->addSpacing(10);
	s2_1->addWidget(MakeText(tr("Size:")));
	s2_1->addWidget(m_font_size_cmb);
	s2_1->addSpacing(10);
	s2_1->addWidget(MakeText(tr("Color:")));
	s2_1->addWidget(m_text_color_cmb);
	s2_1->addStretch(1);
	g2->addLayout(s2_1);
	g2->addWidget(MakeText(tr("Put TrueType font files into the \"Fonts\" folder.\n"
		"Restart VVDViewer to load new font files.")));

	// 4D Script
	QGroupBox* group3 = new QGroupBox(tr("4D Script"));
	QVBoxLayout* g3 = new QVBoxLayout(group3);
	m_run_script_chk = new QCheckBox(tr("Enable execution of a script on 4D data during playback."));
	QHBoxLayout* s3_1 = new QHBoxLayout;
	m_script_file_text = new QLineEdit;
	m_script_file_btn = new QPushButton(tr("Browse..."));
	s3_1->addWidget(MakeText(tr("Script File:")));
	s3_1->addWidget(m_script_file_text, 1);
	s3_1->addWidget(m_script_file_btn);
	g3->addWidget(m_run_script_chk);
	g3->addLayout(s3_1);

	// Paint History
	QGroupBox* group4 = new QGroupBox(tr("Paint History"));
	QVBoxLayout* g4 = new QVBoxLayout(group4);
	QHBoxLayout* s4_1 = new QHBoxLayout;
	m_paint_hist_depth_sldr = MakeSlider(0, 0, 10);
	m_paint_hist_depth_text = new QLineEdit("0");
	m_paint_hist_depth_text->setValidator(new QIntValidator(0, 1000000, m_paint_hist_depth_text));
	FitWidth(m_paint_hist_depth_text, "0000");
	s4_1->addWidget(m_paint_hist_depth_sldr, 1);
	s4_1->addWidget(m_paint_hist_depth_text);
	g4->addLayout(s4_1);
	g4->addWidget(MakeText(tr("The number of undo steps for paint brush selection.\n"
		"Set the value to 0 to disable history.\n"
		"A value greater than 0 slows down speed and increases memory usage.")));

	QVBoxLayout* v = new QVBoxLayout(page);
	v->addWidget(group1);
	v->addWidget(group2);
	v->addWidget(group3);
	v->addWidget(group4);
	v->addStretch(1);

	// ---- events ----
	connect(m_prj_save_chk, &QCheckBox::toggled, this, [this](bool on) { m_prj_save = on; });
	connect(m_realtime_cmp_chk, &QCheckBox::toggled, this, [this](bool on) {
		m_realtime_compress = on;
		// wx: VRenderFrame::SetRealtimeCompression -> DataManager on load
		if (m_data_mgr) m_data_mgr->SetCompression(m_realtime_compress);
	});
	connect(m_font_cmb, QOverload<int>::of(&QComboBox::activated), this, [this](int) {
		const QString str = m_font_cmb->currentText();
		if (str.isEmpty()) return;
		m_font_file = str + ".ttf";
		Q_EMIT fontChanged(GetFontPath(), m_text_size);
	});
	connect(m_font_size_cmb, QOverload<int>::of(&QComboBox::activated), this, [this](int) {
		bool ok = false;
		const int size = m_font_size_cmb->currentText().toInt(&ok);
		if (!ok) return;
		m_text_size = size;
		Q_EMIT textSizeChanged(m_text_size);
	});
	connect(m_text_color_cmb, QOverload<int>::of(&QComboBox::activated), this, [this](int idx) {
		m_text_color = idx;
		Q_EMIT settingsChanged();
	});
	connect(m_run_script_chk, &QCheckBox::toggled, this, [this](bool on) {
		m_run_script = on;
		Q_EMIT settingsChanged();
	});
	connect(m_script_file_text, &QLineEdit::textChanged, this, [this](const QString& s) {
		m_script_file = s;
	});
	connect(m_script_file_btn, &QPushButton::clicked, this, &SettingsDialog::OnScriptFileBtn);
	connect(m_paint_hist_depth_sldr, &QSlider::valueChanged, this, [this](int v) {
		m_paint_hist_depth_text->setText(QString::number(v));
	});
	connect(m_paint_hist_depth_text, &QLineEdit::textChanged, this, [this](const QString& s) {
		bool ok = false;
		unsigned long ival = s.trimmed().toULong(&ok);
		if (!ok) ival = 0;
		{ SignalBlock b(m_paint_hist_depth_sldr); m_paint_hist_depth_sldr->setValue((int)ival); }
		m_paint_hist_depth = (int)ival;
		FLIVR::Texture::mask_undo_num_ = (size_t)m_paint_hist_depth;   // wx SetTextureUndos()
	});

	return page;
}

QWidget* SettingsDialog::CreateRenderingPage()
{
	QWidget* page = new QWidget;

	// Micro Blending
	QGroupBox* group1 = new QGroupBox(tr("Micro Blending"));
	QVBoxLayout* g1 = new QVBoxLayout(group1);
	m_micro_blend_chk = new QCheckBox(tr("Enable Micro Blending"));
	g1->addWidget(m_micro_blend_chk);
	g1->addWidget(MakeText(tr("Micro Blending works only on multiple channels in the depth mode.\n"
		"Enable Micro Blending to render the colors of the colocalized voxels correctly.\n"
		"On certain systems, it may slow down the rendering speed significantly.")));

	// Depth Attenuation
	QGroupBox* groupf = new QGroupBox(tr("Depth Attenuation"));
	QVBoxLayout* gf = new QVBoxLayout(groupf);
	m_use_fog_mesh_chk = new QCheckBox(tr("Enable depth attenuation on meshes"));
	gf->addWidget(m_use_fog_mesh_chk);

	// Mesh Transparency Quality
	QGroupBox* group2 = new QGroupBox(tr("Mesh Transparency Quality"));
	QVBoxLayout* g2 = new QVBoxLayout(group2);
	QHBoxLayout* s2_1 = new QHBoxLayout;
	m_peeling_layers_sldr = MakeSlider(1, 1, 10);
	m_peeling_layers_text = new QLineEdit("1");
	m_peeling_layers_text->setValidator(new QIntValidator(1, 10, m_peeling_layers_text));
	FitWidth(m_peeling_layers_text, "0000");
	s2_1->addWidget(m_peeling_layers_sldr, 1);
	s2_1->addWidget(m_peeling_layers_text);
	g2->addLayout(s2_1);
	g2->addWidget(MakeText(tr("The number of depth peeling layers for rendering transparent mesh objects.\n"
		"Set higher numbers only for complex geometries.\n"
		"It slows down the rendering speed.")));

	// Shadow Direction
	QGroupBox* group3 = new QGroupBox(tr("Shadow Direction"));
	QVBoxLayout* g3 = new QVBoxLayout(group3);
	QHBoxLayout* s3_1 = new QHBoxLayout;
	m_shadow_dir_chk = new QCheckBox(tr("Enable directional shadow"));
	m_shadow_dir_sldr = MakeSlider(-45, -180, 180);
	m_shadow_dir_text = new QLineEdit("-45");
	{
		QDoubleValidator* dv = new QDoubleValidator(-180.0, 180.0, 2, m_shadow_dir_text);
		dv->setLocale(QLocale::c());
		dv->setNotation(QDoubleValidator::StandardNotation);
		m_shadow_dir_text->setValidator(dv);
	}
	FitWidth(m_shadow_dir_text, "-000.00");
	s3_1->addWidget(m_shadow_dir_chk);
	s3_1->addWidget(m_shadow_dir_sldr, 1);
	s3_1->addWidget(m_shadow_dir_text);
	g3->addLayout(s3_1);
	g3->addWidget(MakeText(tr("The direction of the shadows, when shadow is enabled for volume data.")));

	// Gradient Background | Rotations
	QGroupBox* group4 = new QGroupBox(tr("Gradient Background"));
	QVBoxLayout* g4 = new QVBoxLayout(group4);
	m_grad_bg_chk = new QCheckBox(tr("Enable gradient background"));
	g4->addWidget(m_grad_bg_chk);
	QGroupBox* group5 = new QGroupBox(tr("Rotations"));
	QVBoxLayout* g5 = new QVBoxLayout(group5);
	m_rot_link_chk = new QCheckBox(tr("Link all rendering views' rotations."));
	g5->addWidget(m_rot_link_chk);
	QHBoxLayout* g4_5 = new QHBoxLayout;
	g4_5->addWidget(group4);
	g4_5->addStretch(1);
	g4_5->addWidget(group5);

	QVBoxLayout* v = new QVBoxLayout(page);
	v->addWidget(group1);
	v->addWidget(groupf);
	v->addWidget(group2);
	v->addWidget(group3);
	v->addLayout(g4_5);
	v->addStretch(1);

	// ---- events ----
	connect(m_micro_blend_chk, &QCheckBox::toggled, this, [this](bool on) {
		m_micro_blend = on;
		Q_EMIT settingsChanged();
	});
	connect(m_use_fog_mesh_chk, &QCheckBox::toggled, this, [this](bool on) {
		m_use_fog_mesh = on;
		Q_EMIT settingsChanged();
	});
	connect(m_peeling_layers_sldr, &QSlider::valueChanged, this, [this](int v) {
		m_peeling_layers_text->setText(QString::number(v));
	});
	connect(m_peeling_layers_text, &QLineEdit::textChanged, this, [this](const QString& s) {
		bool ok = false;
		long ival = s.trimmed().toLong(&ok);
		if (!ok || ival <= 0) return;
		{ SignalBlock b(m_peeling_layers_sldr); m_peeling_layers_sldr->setValue((int)ival); }
		m_peeling_layers = (int)ival;
		Q_EMIT settingsChanged();
	});
	connect(m_shadow_dir_chk, &QCheckBox::toggled, this, [this](bool on) {
		m_shadow_dir_sldr->setEnabled(on);
		m_shadow_dir_text->setEnabled(on);
		if (on)
		{
			SetShadowDir(ToDouble(m_shadow_dir_text->text()));
			m_shadow_dir = true;
		}
		else
		{
			m_shadow_dir_x = 0.0;
			m_shadow_dir_y = 0.0;
			m_shadow_dir = false;
		}
		Q_EMIT settingsChanged();
	});
	connect(m_shadow_dir_sldr, &QSlider::valueChanged, this, [this](int v) {
		m_shadow_dir_text->setText(QString::asprintf("%.2f", double(v)));
	});
	connect(m_shadow_dir_text, &QLineEdit::textChanged, this, [this](const QString& s) {
		const double deg = ToDouble(s);
		{ SignalBlock b(m_shadow_dir_sldr); m_shadow_dir_sldr->setValue(int(deg)); }
		SetShadowDir(deg);
		Q_EMIT settingsChanged();
	});
	connect(m_grad_bg_chk, &QCheckBox::toggled, this, [this](bool on) {
		m_grad_bg = on;
		Q_EMIT settingsChanged();
	});
	connect(m_rot_link_chk, &QCheckBox::toggled, this, [this](bool on) {
		Q_EMIT rotationLinkChanged(on);
	});

	return page;
}

QWidget* SettingsDialog::CreatePerformancePage()
{
	QWidget* page = new QWidget;

	// Variable Sample Rate
	QGroupBox* group1 = new QGroupBox(tr("Variable Sample Rate"));
	QVBoxLayout* g1 = new QVBoxLayout(group1);
	m_mouse_int_chk = new QCheckBox(tr("Reduce volume sample rate for mouse interactions."));
	m_mouse_lv_chk = new QCheckBox(tr("Down volume resolution for mouse interactions."));
	g1->addWidget(m_mouse_int_chk);
	g1->addWidget(m_mouse_lv_chk);

	// Large Data Streaming
	QGroupBox* group2 = new QGroupBox(tr("Large Data Streaming"));
	QVBoxLayout* g2 = new QVBoxLayout(group2);
	m_streaming_chk = new QCheckBox(tr("Enable streaming for large datasets."));
	g2->addWidget(m_streaming_chk);
	QGridLayout* grid = new QGridLayout;
	grid->setColumnStretch(1, 1);
	auto addRow = [grid](int row, const QString& label, QSlider* s, QLineEdit* t, const QString& unit) {
		grid->addWidget(MakeText(label), row, 0);
		grid->addWidget(s, row, 1);
		grid->addWidget(t, row, 2);
		grid->addWidget(MakeText(unit), row, 3);
	};
	m_graphics_mem_sldr = MakeSlider(10, 1, 100);
	m_graphics_mem_text = new QLineEdit("1000");
	m_large_data_sldr = MakeSlider(20, 5, 200);
	m_large_data_text = new QLineEdit("200");
	m_block_size_sldr = MakeSlider(7, 4, 11);
	m_block_size_text = new QLineEdit("128");
	m_response_time_sldr = MakeSlider(10, 1, 100);
	m_response_time_text = new QLineEdit("100");
	for (QLineEdit* e : { m_graphics_mem_text, m_large_data_text, m_block_size_text, m_response_time_text })
	{
		e->setValidator(new QIntValidator(0, 100000000, e));
		FitWidth(e, "000000");
	}
	addRow(0, tr("Graphics Memory:"), m_graphics_mem_sldr, m_graphics_mem_text, tr("MB"));
	addRow(1, tr("Large Data Size:"), m_large_data_sldr, m_large_data_text, tr("MB"));
	addRow(2, tr("Brick Size:"), m_block_size_sldr, m_block_size_text, tr("vx"));
	addRow(3, tr("Response Time:"), m_response_time_sldr, m_response_time_text, tr("ms"));
	g2->addLayout(grid);
	g2->addWidget(MakeText(tr("Data streaming allows rendering datasets of much larger size than available\n"
		"graphics memory. Datasets are divided into small blocks. Then, these blocks\n"
		"are loaded into graphics memory and rendered in sequence.\n"
		"Restart is needed for the settings to take effect.")));

	// Main Memory Buffer
	QGroupBox* group3 = new QGroupBox(tr("Main Memory Buffer"));
	QVBoxLayout* g3 = new QVBoxLayout(group3);
	QGridLayout* grid3 = new QGridLayout;
	grid3->setColumnStretch(1, 1);
	m_main_mem_buf_sldr = MakeSlider(40, 0, 200);
	m_main_mem_buf_text = new QLineEdit("4000");
	m_main_mem_buf_text->setValidator(new QIntValidator(0, 100000000, m_main_mem_buf_text));
	FitWidth(m_main_mem_buf_text, "000000");
	grid3->addWidget(MakeText(tr("Buffer Size:")), 0, 0);
	grid3->addWidget(m_main_mem_buf_sldr, 0, 1);
	grid3->addWidget(m_main_mem_buf_text, 0, 2);
	grid3->addWidget(MakeText(tr("MB")), 0, 3);
	g3->addLayout(grid3);
	// keep the label column of both groups the same width (wx: 110 px labels)
	{
		int w = 0;
		for (QGridLayout* gl : { grid, grid3 })
			for (int r = 0; r < gl->rowCount(); ++r)
				if (QLayoutItem* it = gl->itemAtPosition(r, 0))
					if (it->widget()) w = qMax(w, it->widget()->sizeHint().width());
		grid->setColumnMinimumWidth(0, w);
		grid3->setColumnMinimumWidth(0, w);
		int tw = m_graphics_mem_text->minimumWidth();
		grid->setColumnMinimumWidth(3, m_graphics_mem_text->fontMetrics().horizontalAdvance("MB") + 4);
		grid3->setColumnMinimumWidth(3, m_graphics_mem_text->fontMetrics().horizontalAdvance("MB") + 4);
		grid->setColumnMinimumWidth(2, tw);
		grid3->setColumnMinimumWidth(2, tw);
	}

	QVBoxLayout* v = new QVBoxLayout(page);
	v->addWidget(group1);
	v->addWidget(group2);
	v->addWidget(group3);
	v->addStretch(1);

	// ---- events ----
	connect(m_mouse_int_chk, &QCheckBox::toggled, this, [this](bool on) {
		m_mouse_int = on;
		Q_EMIT settingsChanged();
	});
	connect(m_mouse_lv_chk, &QCheckBox::toggled, this, [this](bool on) {
		m_mouse_lv = on;
		Q_EMIT settingsChanged();
	});
	connect(m_streaming_chk, &QCheckBox::toggled, this, [this](bool on) {
		m_mem_swap = on;
		EnableStreaming(m_mem_swap);
	});

	connect(m_graphics_mem_sldr, &QSlider::valueChanged, this, [this](int v) {
		m_graphics_mem_text->setText(QString::number(v * 100));
	});
	connect(m_graphics_mem_text, &QLineEdit::textChanged, this, [this](const QString& s) {
		const double val = ToDouble(s);
		if (val <= 0.0) return;
		{ SignalBlock b(m_graphics_mem_sldr); m_graphics_mem_sldr->setValue(int(val / 100.0)); }
		m_graphics_mem = val;
	});
	connect(m_large_data_sldr, &QSlider::valueChanged, this, [this](int v) {
		m_large_data_text->setText(QString::number(v * 10));
	});
	connect(m_large_data_text, &QLineEdit::textChanged, this, [this](const QString& s) {
		bool ok = false;
		const double val = ToDouble(s, &ok);
		if (!ok || val < 0.0) return;
		{ SignalBlock b(m_large_data_sldr); m_large_data_sldr->setValue(int(val / 10.0)); }
		m_large_data_size = val;
	});
	connect(m_block_size_sldr, &QSlider::valueChanged, this, [this](int v) {
		m_block_size_text->setText(QString::number(2 << (v - 1)));
	});
	connect(m_block_size_text, &QLineEdit::textChanged, this, [this](const QString& s) {
		const double val = ToDouble(s);
		if (val <= 0.0) return;
		{ SignalBlock b(m_block_size_sldr); m_block_size_sldr->setValue(int(std::log(val) / std::log(2.0) + 0.5)); }
		m_force_brick_size = int(val);
	});
	connect(m_response_time_sldr, &QSlider::valueChanged, this, [this](int v) {
		m_response_time_text->setText(QString::number(v * 10));
	});
	connect(m_response_time_text, &QLineEdit::textChanged, this, [this](const QString& s) {
		const double val = ToDouble(s);
		if (val <= 0.0) return;
		{ SignalBlock b(m_response_time_sldr); m_response_time_sldr->setValue(int(val / 10.0)); }
		m_up_time = int(val);
	});
	connect(m_main_mem_buf_sldr, &QSlider::valueChanged, this, [this](int v) {
		m_main_mem_buf_text->setText(QString::number(v * 100));
	});
	connect(m_main_mem_buf_text, &QLineEdit::textChanged, this, [this](const QString& s) {
		const double val = ToDouble(s);
		if (val <= 0.0) return;
		{ SignalBlock b(m_main_mem_buf_sldr); m_main_mem_buf_sldr->setValue(int(val / 100.0)); }
		m_main_mem_buf_size = val;
	});

	return page;
}

QWidget* SettingsDialog::CreateFormatPage()
{
	QWidget* page = new QWidget;

	// Override Voxel Size
	QGroupBox* group1 = new QGroupBox(tr("Override Voxel Size"));
	QVBoxLayout* g1 = new QVBoxLayout(group1);
	m_override_vox_chk = new QCheckBox(tr("Get voxel size info from the first opened dataset."));
	g1->addWidget(m_override_vox_chk);

	// Default Colors for Excitation Wavelengths
	QGroupBox* group2 = new QGroupBox(
		tr("Default Colors for Excitation Wavelengths (nm) (for OIB/OIF/LSM files)"));
	QVBoxLayout* g2 = new QVBoxLayout(group2);
	QGridLayout* grid = new QGridLayout;
	grid->setHorizontalSpacing(6);
	QComboBox** cmbs[4] = { &m_wav_color1_cmb, &m_wav_color2_cmb, &m_wav_color3_cmb, &m_wav_color4_cmb };
	// row 0: one combo above each "--" between two wavelength ranges
	for (int i = 0; i < 4; ++i)
	{
		QComboBox* c = new QComboBox;
		c->addItem(tr("Red"));
		c->addItem(tr("Green"));
		c->addItem(tr("Blue"));
		c->addItem(tr("Purple"));
		c->addItem(tr("White"));
		*cmbs[i] = c;
		grid->addWidget(c, 0, 2 * i + 1, Qt::AlignHCenter);
	}
	// row 1: 350 -- 450/480/488 -- 543/568 -- 633 -- 700
	const char* labels[9] = { "350", "--", "450\n480\n488", "--", "543\n568", "--", "633", "--", "700" };
	for (int i = 0; i < 9; ++i)
	{
		QLabel* l = MakeText(QString::fromLatin1(labels[i]));
		l->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
		grid->addWidget(l, 1, i, Qt::AlignHCenter | Qt::AlignTop);
	}
	QHBoxLayout* h = new QHBoxLayout;
	h->addLayout(grid);
	h->addStretch(1);
	g2->addLayout(h);

	QVBoxLayout* v = new QVBoxLayout(page);
	v->addWidget(group1);
	v->addWidget(group2);
	v->addStretch(1);

	// ---- events ----
	connect(m_override_vox_chk, &QCheckBox::toggled, this, [this](bool on) {
		m_override_vox = on;
		if (m_data_mgr) m_data_mgr->SetOverrideVox(m_override_vox);
	});
	int* members[4] = { &m_wav_color1, &m_wav_color2, &m_wav_color3, &m_wav_color4 };
	for (int i = 0; i < 4; ++i)
	{
		QComboBox* c = *cmbs[i];
		int* m = members[i];
		connect(c, QOverload<int>::of(&QComboBox::activated), this, [this, m](int idx) {
			*m = idx + 1;
			ApplyWavelengthColors();
		});
	}

	return page;
}

// ---------------------------------------------------------------------------

SettingsDialog::SettingsDialog(QWidget* parent)
	: QDialog(parent)
{
	setWindowTitle(tr("Settings"));
	setModal(false);

	SetDefaults();

	QTabWidget* notebook = new QTabWidget;
	notebook->addTab(CreateProjectPage(), tr("Project"));
	notebook->addTab(CreateRenderingPage(), tr("Rendering"));
	notebook->addTab(CreatePerformancePage(), tr("Performance"));
	notebook->addTab(CreateFormatPage(), tr("File format"));

	m_save_btn = new QPushButton(tr("Done"));
	m_close_btn = new QPushButton(tr("Cancel"));
	m_save_btn->setDefault(true);
	QHBoxLayout* group_b = new QHBoxLayout;
	group_b->addWidget(m_close_btn);
	group_b->addStretch(1);
	group_b->addWidget(m_save_btn);

	QVBoxLayout* v = new QVBoxLayout(this);
	v->addWidget(notebook, 1);
	v->addLayout(group_b);

	connect(m_save_btn, &QPushButton::clicked, this, &SettingsDialog::OnDone);
	connect(m_close_btn, &QPushButton::clicked, this, &SettingsDialog::OnCancel);

	LoadSettings();
}

SettingsDialog::~SettingsDialog() = default;

void SettingsDialog::SetDataManager(DataManager* mgr)
{
	m_data_mgr = mgr;
	if (!m_data_mgr)
		return;
	m_data_mgr->SetOverrideVox(m_override_vox);
	m_data_mgr->SetPvxmlFlipX(m_pvxml_flip_x);
	m_data_mgr->SetPvxmlFlipY(m_pvxml_flip_y);
	m_data_mgr->SetCompression(m_realtime_compress);
	m_data_mgr->SetSkipBrick(m_skip_bricks);
	m_data_mgr->SetTimeId(ToVVD(m_time_id));
	if (GetTestMode(3))
		m_data_mgr->m_vol_test_wiref = true;
	ApplyWavelengthColors();
}

QString SettingsDialog::SettingsFilePath(bool forSave)
{
	const VVDString name(SETTING_FILE_NAME_QT);
	const VVDString expath = vvd::GetExecutableDir();
#if defined(__APPLE__)
	(void)forSave;
	return FromVVD(expath + L"/../Resources/" + name);
#else
	VVDString dft = vvd::JoinPath(expath, name);
	const VVDString dft2 = vvd::JoinPath(vvd::GetUserConfigDir(), name);
	if (forSave)
	{
		if (!vvd::FileExists(dft) && vvd::FileExists(dft2))
			dft = dft2;
	}
	else if (!vvd::FileExists(dft))
		dft = dft2;
	return FromVVD(dft);
#endif
}

QString SettingsDialog::FontsDir()
{
	return QDir(FromVVD(vvd::GetResourcesDir())).filePath("Fonts");
}

QString SettingsDialog::GetFontPath() const
{
	QDir dir(FontsDir());
	return dir.filePath(m_font_file.isEmpty() ? QString("FreeSans.ttf") : m_font_file);
}

void SettingsDialog::SetDefaults()
{
	m_gmc_mode = 2;
	m_prj_save = false;
	m_realtime_compress = false;
	m_skip_bricks = false;
	m_save_compress = true;
	m_test_speed = false;
	m_test_param = false;
	m_test_wiref = false;
	m_peeling_layers = 1;
	m_micro_blend = false;
	m_shadow_dir = false;
	m_shadow_dir_x = 0.0;
	m_shadow_dir_y = 0.0;
	m_mouse_int = true;
	m_mouse_lv = false;
	m_wav_color1 = 5;
	m_wav_color2 = 5;
	m_wav_color3 = 5;
	m_wav_color4 = 5;
	m_time_id = "_T";
	m_grad_bg = false;
	m_override_vox = true;
	m_soft_threshold = 0.0;
	m_run_script = false;
	m_script_file = "";
	m_text_size = 12;
	m_text_color = 0;
	m_font_file = "";
	m_mem_swap = false;
	m_graphics_mem = 1000.0;
	m_main_mem_buf_size = 4000.0;
	m_large_data_size = 1000.0;
	m_force_brick_size = 128;
	m_up_time = 100;
	m_update_order = 0;
	m_point_volume_mode = 0;
	m_ruler_use_transf = false;
	m_ruler_time_dep = true;
	m_ruler_auto_select = false;
	m_cell_warp = rigidcells::Params();
	m_cell_warp.device = -1;   // the plugin's default: the first GPU if there is one
	m_pvxml_flip_x = false;
	m_pvxml_flip_y = false;
	m_red_bit = 8;
	m_green_bit = 8;
	m_blue_bit = 8;
	m_alpha_bit = 8;
	m_depth_bit = 24;
	m_samples = 0;
	m_gl_major_ver = 4;
	m_gl_minor_ver = 4;
	m_gl_profile_mask = 2;
	m_cl_device_id = 0;
	m_paint_hist_depth = 0;
	m_stay_top = false;
	m_show_cursor = true;
	m_use_fog_mesh = false;
}

void SettingsDialog::LoadSettings()
{
	SetDefaults();

	const VVDString path = ToVVD(SettingsFilePath(false));
	vvd::IniFile cfg;
	if (!vvd::FileExists(path) || !cfg.Load(path))
	{
		UpdateUI();
		return;
	}

	VVDString s;
	if (HasGroup(cfg, L"/gm calculation"))
		m_gmc_mode = (int)cfg.ReadLong(L"/gm calculation/mode", 2);
	if (HasGroup(cfg, L"/peeling layers"))
		m_peeling_layers = (int)cfg.ReadLong(L"/peeling layers/value", 1);
	if (HasGroup(cfg, L"/micro blend"))
		m_micro_blend = cfg.ReadBool(L"/micro blend/mode", false);
	if (HasGroup(cfg, L"/save project"))
		m_prj_save = cfg.ReadBool(L"/save project/mode", false);
	if (HasGroup(cfg, L"/realtime compress"))
		m_realtime_compress = cfg.ReadBool(L"/realtime compress/mode", false);
	if (HasGroup(cfg, L"/skip bricks"))
		m_skip_bricks = cfg.ReadBool(L"/skip bricks/mode", false);
	if (HasGroup(cfg, L"/mouse int"))
	{
		m_mouse_int = cfg.ReadBool(L"/mouse int/mode", true);
		m_mouse_lv = cfg.ReadBool(L"/mouse int/res_mode", true);
	}
	if (HasGroup(cfg, L"/dir shadow"))
	{
		m_shadow_dir = cfg.ReadBool(L"/dir shadow/mode", false);
		m_shadow_dir_x = cfg.ReadDouble(L"/dir shadow/x", 0.0);
		m_shadow_dir_y = cfg.ReadDouble(L"/dir shadow/y", 0.0);
	}
	if (HasGroup(cfg, L"/test mode"))
	{
		m_test_speed = cfg.ReadBool(L"/test mode/speed", false);
		m_test_param = cfg.ReadBool(L"/test mode/param", false);
		m_test_wiref = cfg.ReadBool(L"/test mode/wiref", false);
	}
	if (HasGroup(cfg, L"/wavelength to color"))
	{
		m_wav_color1 = (int)cfg.ReadLong(L"/wavelength to color/c1", 5);
		m_wav_color2 = (int)cfg.ReadLong(L"/wavelength to color/c2", 5);
		m_wav_color3 = (int)cfg.ReadLong(L"/wavelength to color/c3", 5);
		m_wav_color4 = (int)cfg.ReadLong(L"/wavelength to color/c4", 5);
	}
	if (cfg.Read(L"/time id/value", s)) m_time_id = FromVVD(s);
	cfg.Read(L"/grad bg/value", m_grad_bg);
	cfg.Read(L"/save cmp/value", m_save_compress);
	cfg.Read(L"/override vox/value", m_override_vox);
	cfg.Read(L"/soft threshold/value", m_soft_threshold);
	cfg.Read(L"/run script/value", m_run_script);
	if (cfg.Read(L"/run script/file", s)) m_script_file = FromVVD(s);
	cfg.Read(L"/paint history/value", m_paint_hist_depth);
	if (cfg.Read(L"/text font/file", s)) m_font_file = FromVVD(s);
	cfg.Read(L"/text font/value", m_text_size);
	cfg.Read(L"/text font/color", m_text_color);
	cfg.Read(L"/full screen/stay top", m_stay_top);
	cfg.Read(L"/full screen/show cursor", m_show_cursor);
	cfg.Read(L"/memory settings/mem swap", m_mem_swap);
	cfg.Read(L"/memory settings/graphics mem", m_graphics_mem);
	cfg.Read(L"/memory settings/main memory buffer size", m_main_mem_buf_size);
	cfg.Read(L"/memory settings/large data size", m_large_data_size);
	cfg.Read(L"/memory settings/force brick size", m_force_brick_size);
	cfg.Read(L"/memory settings/up time", m_up_time);
	cfg.Read(L"/update order/value", m_update_order);
	cfg.Read(L"/point volume mode/value", m_point_volume_mode);
	cfg.Read(L"/ruler use transf/value", m_ruler_use_transf);
	// wx reads "/ruler time dep" but writes "/ruler time dependent"; accept both
	if (!cfg.Read(L"/ruler time dep/value", m_ruler_time_dep))
		cfg.Read(L"/ruler time dependent/value", m_ruler_time_dep);
	cfg.Read(L"/ruler auto select/value", m_ruler_auto_select);
	cfg.Read(L"/rigid cells/local", m_cell_warp.local_mode);
	cfg.Read(L"/rigid cells/per label", m_cell_warp.per_label);
	cfg.Read(L"/rigid cells/beta", m_cell_warp.beta);
	cfg.Read(L"/rigid cells/sigma min", m_cell_warp.sigma_min);
	cfg.Read(L"/rigid cells/neurite", m_cell_warp.neurite);
	cfg.Read(L"/rigid cells/samples", m_cell_warp.samples);
	cfg.Read(L"/rigid cells/min voxels", m_cell_warp.min_voxels);
	cfg.Read(L"/rigid cells/radius", m_cell_warp.radius);
	cfg.Read(L"/rigid cells/device", m_cell_warp.device);
	cfg.Read(L"/rigid cells/loo", m_cell_warp.loo);
	cfg.Read(L"/pvxml flip/x", m_pvxml_flip_x);
	cfg.Read(L"/pvxml flip/y", m_pvxml_flip_y);
	cfg.Read(L"/pixel format/red_bit", m_red_bit);
	cfg.Read(L"/pixel format/green_bit", m_green_bit);
	cfg.Read(L"/pixel format/blue_bit", m_blue_bit);
	cfg.Read(L"/pixel format/alpha_bit", m_alpha_bit);
	cfg.Read(L"/pixel format/depth_bit", m_depth_bit);
	cfg.Read(L"/pixel format/samples", m_samples);
	cfg.Read(L"/context attrib/gl_major_ver", m_gl_major_ver);
	cfg.Read(L"/context attrib/gl_minor_ver", m_gl_minor_ver);
	cfg.Read(L"/context attrib/gl_profile_mask", m_gl_profile_mask);
	cfg.Read(L"/cl device/device_id", m_cl_device_id);
	cfg.Read(L"/use fog mesh/use_fog_mesh", m_use_fog_mesh);

	UpdateUI();
}

void SettingsDialog::UpdateUI()
{
	QList<QObject*> ctrls = {
		m_prj_save_chk, m_realtime_cmp_chk, m_font_cmb, m_font_size_cmb, m_text_color_cmb,
		m_run_script_chk, m_script_file_text, m_paint_hist_depth_sldr, m_paint_hist_depth_text,
		m_micro_blend_chk, m_use_fog_mesh_chk, m_peeling_layers_sldr, m_peeling_layers_text,
		m_shadow_dir_chk, m_shadow_dir_sldr, m_shadow_dir_text, m_grad_bg_chk, m_rot_link_chk,
		m_mouse_int_chk, m_mouse_lv_chk, m_streaming_chk,
		m_graphics_mem_sldr, m_graphics_mem_text, m_large_data_sldr, m_large_data_text,
		m_block_size_sldr, m_block_size_text, m_response_time_sldr, m_response_time_text,
		m_main_mem_buf_sldr, m_main_mem_buf_text,
		m_override_vox_chk, m_wav_color1_cmb, m_wav_color2_cmb, m_wav_color3_cmb, m_wav_color4_cmb };
	std::vector<bool> old;
	old.reserve(ctrls.size());
	for (QObject* o : ctrls) old.push_back(o->blockSignals(true));

	m_prj_save_chk->setChecked(m_prj_save);
	m_realtime_cmp_chk->setChecked(m_realtime_compress);
	m_mouse_int_chk->setChecked(m_mouse_int);
	m_mouse_lv_chk->setChecked(m_mouse_lv);
	m_peeling_layers_sldr->setValue(m_peeling_layers);
	m_peeling_layers_text->setText(QString::number(m_peeling_layers));
	m_micro_blend_chk->setChecked(m_micro_blend);
	// shadow direction
	m_shadow_dir_chk->setChecked(m_shadow_dir);
	m_shadow_dir_sldr->setEnabled(m_shadow_dir);
	m_shadow_dir_text->setEnabled(m_shadow_dir);
	if (!m_shadow_dir)
	{
		m_shadow_dir_x = 0.0;
		m_shadow_dir_y = 0.0;
	}
	const double deg = GetShadowDirDeg();
	m_shadow_dir_sldr->setValue(int(deg + 0.5));
	m_shadow_dir_text->setText(QString::asprintf("%.2f", deg));
	m_grad_bg_chk->setChecked(m_grad_bg);
	m_override_vox_chk->setChecked(m_override_vox);
	// wavelength to color
	m_wav_color1_cmb->setCurrentIndex(m_wav_color1 - 1);
	m_wav_color2_cmb->setCurrentIndex(m_wav_color2 - 1);
	m_wav_color3_cmb->setCurrentIndex(m_wav_color3 - 1);
	m_wav_color4_cmb->setCurrentIndex(m_wav_color4 - 1);
	// font
	{
		QString str = m_font_file;
		const int dot = str.lastIndexOf('.');
		if (dot >= 0) str = str.left(dot);
		const int font_sel = m_font_cmb->findText(str);
		if (font_sel >= 0)
			m_font_cmb->setCurrentIndex(font_sel);
		for (int i = 0; i < m_font_size_cmb->count(); ++i)
		{
			bool ok = false;
			const int fs = m_font_size_cmb->itemText(i).toInt(&ok);
			if (ok && fs <= m_text_size)
				m_font_size_cmb->setCurrentIndex(i);
		}
		m_text_color_cmb->setCurrentIndex(m_text_color);
	}
	// script
	m_run_script_chk->setChecked(m_run_script);
	m_script_file_text->setText(m_script_file);
	// paint history depth
	m_paint_hist_depth_text->setText(QString::number(m_paint_hist_depth));
	m_paint_hist_depth_sldr->setValue(m_paint_hist_depth);
	// memory settings
	m_streaming_chk->setChecked(m_mem_swap);
	m_graphics_mem_text->setText(QString::number((int)m_graphics_mem));
	m_graphics_mem_sldr->setValue(int(m_graphics_mem / 100.0));
	m_large_data_text->setText(QString::number((int)m_large_data_size));
	m_large_data_sldr->setValue(int(m_large_data_size / 10.0));
	m_block_size_text->setText(QString::number(m_force_brick_size));
	if (m_force_brick_size > 0)
		m_block_size_sldr->setValue(int(std::log((double)m_force_brick_size) / std::log(2.0) + 0.5));
	m_response_time_text->setText(QString::number(m_up_time));
	m_response_time_sldr->setValue(int(m_up_time / 10.0));
	m_main_mem_buf_text->setText(QString::number((int)m_main_mem_buf_size));
	m_main_mem_buf_sldr->setValue(int(m_main_mem_buf_size / 100.0));
	m_use_fog_mesh_chk->setChecked(m_use_fog_mesh);

	for (int i = 0; i < ctrls.size(); ++i) ctrls[i]->blockSignals(old[i]);

	EnableStreamingUI(m_mem_swap);
	Q_EMIT settingsChanged();
}

void SettingsDialog::SaveSettings()
{
	vvd::IniFile cfg;

	cfg.Write(L"ver_major", VVDString(VERSION_MAJOR_TAG));
	cfg.Write(L"ver_minor", VVDString(VERSION_MINOR_TAG));

	cfg.Write(L"/gm calculation/mode", m_gmc_mode);
	cfg.Write(L"/peeling layers/value", m_peeling_layers);
	cfg.Write(L"/micro blend/mode", m_micro_blend);
	cfg.Write(L"/save project/mode", m_prj_save);
	cfg.Write(L"/realtime compress/mode", m_realtime_compress);
	cfg.Write(L"/skip bricks/mode", m_skip_bricks);
	cfg.Write(L"/mouse int/mode", m_mouse_int);
	cfg.Write(L"/mouse int/res_mode", m_mouse_lv);
	cfg.Write(L"/dir shadow/mode", m_shadow_dir);
	cfg.Write(L"/dir shadow/x", m_shadow_dir_x);
	cfg.Write(L"/dir shadow/y", m_shadow_dir_y);
	cfg.Write(L"/test mode/speed", m_test_speed);
	cfg.Write(L"/test mode/param", m_test_param);
	cfg.Write(L"/test mode/wiref", m_test_wiref);
	cfg.Write(L"/wavelength to color/c1", m_wav_color1);
	cfg.Write(L"/wavelength to color/c2", m_wav_color2);
	cfg.Write(L"/wavelength to color/c3", m_wav_color3);
	cfg.Write(L"/wavelength to color/c4", m_wav_color4);
	cfg.Write(L"/time id/value", ToVVD(m_time_id));
	cfg.Write(L"/grad bg/value", m_grad_bg);
	cfg.Write(L"/override vox/value", m_override_vox);
	cfg.Write(L"/soft threshold/value", m_soft_threshold);
	cfg.Write(L"/save cmp/value", m_save_compress);
	cfg.Write(L"/run script/value", m_run_script);
	cfg.Write(L"/run script/file", ToVVD(m_script_file));
	cfg.Write(L"/paint history/value", m_paint_hist_depth);
	cfg.Write(L"/text font/file", ToVVD(m_font_file));
	cfg.Write(L"/text font/value", m_text_size);
	cfg.Write(L"/text font/color", m_text_color);
	cfg.Write(L"/full screen/stay top", m_stay_top);
	cfg.Write(L"/full screen/show cursor", m_show_cursor);
	cfg.Write(L"/memory settings/mem swap", m_mem_swap);
	cfg.Write(L"/memory settings/graphics mem", m_graphics_mem);
	cfg.Write(L"/memory settings/large data size", m_large_data_size);
	cfg.Write(L"/memory settings/force brick size", m_force_brick_size);
	cfg.Write(L"/memory settings/up time", m_up_time);
	cfg.Write(L"/memory settings/main memory buffer size", m_main_mem_buf_size);
	cfg.Write(L"/update order/value", m_update_order);
	cfg.Write(L"/point volume mode/value", m_point_volume_mode);
	cfg.Write(L"/ruler use transf/value", m_ruler_use_transf);
	cfg.Write(L"/ruler time dependent/value", m_ruler_time_dep);   // same key as wx SaveSettings
	cfg.Write(L"/ruler auto select/value", m_ruler_auto_select);   // Qt only
	cfg.Write(L"/rigid cells/local", m_cell_warp.local_mode);
	cfg.Write(L"/rigid cells/per label", m_cell_warp.per_label);
	cfg.Write(L"/rigid cells/beta", m_cell_warp.beta);
	cfg.Write(L"/rigid cells/sigma min", m_cell_warp.sigma_min);
	cfg.Write(L"/rigid cells/neurite", m_cell_warp.neurite);
	cfg.Write(L"/rigid cells/samples", m_cell_warp.samples);
	cfg.Write(L"/rigid cells/min voxels", m_cell_warp.min_voxels);
	cfg.Write(L"/rigid cells/radius", m_cell_warp.radius);
	cfg.Write(L"/rigid cells/device", m_cell_warp.device);
	cfg.Write(L"/rigid cells/loo", m_cell_warp.loo);
	cfg.Write(L"/pvxml flip/x", m_pvxml_flip_x);
	cfg.Write(L"/pvxml flip/y", m_pvxml_flip_y);
	cfg.Write(L"/pixel format/red_bit", m_red_bit);
	cfg.Write(L"/pixel format/green_bit", m_green_bit);
	cfg.Write(L"/pixel format/blue_bit", m_blue_bit);
	cfg.Write(L"/pixel format/alpha_bit", m_alpha_bit);
	cfg.Write(L"/pixel format/depth_bit", m_depth_bit);
	cfg.Write(L"/pixel format/samples", m_samples);
	cfg.Write(L"/context attrib/gl_major_ver", m_gl_major_ver);
	cfg.Write(L"/context attrib/gl_minor_ver", m_gl_minor_ver);
	cfg.Write(L"/context attrib/gl_profile_mask", m_gl_profile_mask);
	cfg.Write(L"/cl device/device_id", m_cl_device_id);
	cfg.Write(L"/use fog mesh/use_fog_mesh", m_use_fog_mesh);

	cfg.Save(ToVVD(SettingsFilePath(true)));
}

void SettingsDialog::ApplyToEngine()
{
	// VRenderFrame constructor
	FLIVR::VolumeRenderer::set_soft_threshold(m_soft_threshold);
	FLIVR::MultiVolumeRenderer::set_soft_threshold(m_soft_threshold);
	TreeLayer::SetSoftThreshsold(m_soft_threshold);
	VolumeMeshConv::SetSoftThreshold(m_soft_threshold);
	if (!m_engine_initialized)
	{
		// wx sets these once at start-up; later changes need a restart
		FLIVR::TextureRenderer::set_mainmem_buf_size(m_main_mem_buf_size);
		FLIVR::TextureRenderer::set_available_mainmem_buf_size(m_main_mem_buf_size);
		m_engine_initialized = true;
	}
	// VRenderFrame::SetTextureUndos
	FLIVR::Texture::mask_undo_num_ = (size_t)m_paint_hist_depth;
	// VRenderFrame::SetTextureRendererSettings
	ApplyTextureRendererSettings();
	// DataManager
	SetDataManager(m_data_mgr);
}

void SettingsDialog::ApplyTextureRendererSettings()
{
	FLIVR::TextureRenderer::set_streaming(m_mem_swap);
	// wx m_gpu_max_mem is never detected (-1): the user limit is used as is
	const double mem_size = m_graphics_mem;
	// TextureRenderer takes no account of 2D textures in calculating allocated memory size
	const double mem_delta = mem_size - FLIVR::TextureRenderer::get_mem_limit();
	const double prev_available_mem = FLIVR::TextureRenderer::get_available_mem();
	FLIVR::TextureRenderer::set_use_mem_limit(true);
	FLIVR::TextureRenderer::set_mem_limit(mem_size);
	FLIVR::TextureRenderer::set_available_mem(mem_delta + prev_available_mem);
	FLIVR::TextureRenderer::set_large_data_size(m_large_data_size);
	FLIVR::TextureRenderer::set_force_brick_size(m_force_brick_size);
	FLIVR::TextureRenderer::set_up_time(m_up_time);
	// testing: VVD_QT_UP_TIME=<ms> overrides the streaming time budget for
	// this run only (the setting itself is not changed)
	{
		bool ok = false;
		const int ut = qEnvironmentVariableIntValue("VVD_QT_UP_TIME", &ok);
		if (ok && ut >= 0)
			FLIVR::TextureRenderer::set_up_time((unsigned long)ut);
	}
	FLIVR::TextureRenderer::set_update_order(m_update_order);
}

void SettingsDialog::ApplyWavelengthColors()
{
	if (m_data_mgr && m_wav_color1 && m_wav_color2 && m_wav_color3 && m_wav_color4)
		m_data_mgr->SetWavelengthColor(m_wav_color1, m_wav_color2, m_wav_color3, m_wav_color4);
}

void SettingsDialog::EnableStreamingUI(bool enable)
{
	for (QWidget* w : std::initializer_list<QWidget*>{
		m_graphics_mem_sldr, m_graphics_mem_text, m_large_data_sldr, m_large_data_text,
		m_block_size_sldr, m_block_size_text, m_response_time_sldr, m_response_time_text })
		w->setEnabled(enable);
}

void SettingsDialog::EnableStreaming(bool enable)
{
	EnableStreamingUI(enable);
	// wx: vr_frame->SetTextureRendererSettings(); RefreshVRenderViews();
	ApplyTextureRendererSettings();
	Q_EMIT engineSettingsApplied();
}

bool SettingsDialog::GetTestMode(int type) const
{
	// testing aid: VVD_QT_TEST_MODE="speed,param,wiref" turns modes on for
	// the run (the settings file's [test mode] group is not written)
	const QStringList env = qEnvironmentVariable("VVD_QT_TEST_MODE").split(',', Qt::SkipEmptyParts);
	switch (type)
	{
	case 1: return m_test_speed || env.contains("speed");
	case 2: return m_test_param || env.contains("param");
	case 3: return m_test_wiref || env.contains("wiref");
	default: return false;
	}
}

int SettingsDialog::GetWavelengthColor(int n) const
{
	switch (n)
	{
	case 1: return m_wav_color1;
	case 2: return m_wav_color2;
	case 3: return m_wav_color3;
	case 4: return m_wav_color4;
	default: return 0;
	}
}

bool SettingsDialog::GetRotLink() const
{
	return m_rot_link_chk && m_rot_link_chk->isChecked();
}

void SettingsDialog::SetShadowDir(double deg)
{
	m_shadow_dir_x = std::cos(deg * kPi / 180.0);
	m_shadow_dir_y = std::sin(deg * kPi / 180.0);
}

double SettingsDialog::GetShadowDirDeg() const
{
	return std::atan2(m_shadow_dir_y, m_shadow_dir_x) * 180.0 / kPi;
}

void SettingsDialog::OnDone()
{
	SaveSettings();
	// wx SaveSettings() -> EnableStreaming(m_mem_swap)
	EnableStreaming(m_mem_swap);
	hide();
}

void SettingsDialog::OnCancel()
{
	// wx: only hides the pane; changes already made stay in effect
	hide();
}

void SettingsDialog::OnScriptFileBtn()
{
	const QString file = QFileDialog::getOpenFileName(this,
		tr("Choose a 4D script file"), QString(), tr("4D script file (*.txt)"));
	if (file.isEmpty())
		return;
	m_script_file = file;
	m_script_file_text->setText(m_script_file);
}
