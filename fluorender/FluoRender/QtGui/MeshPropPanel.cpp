#include "MeshPropPanel.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QSlider>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QToolButton>
#include <QLabel>
#include <QColor>
#include <QColorDialog>
#include <QDoubleValidator>
#include <QIntValidator>
#include <QSignalBlocker>
#include <algorithm>

#include "DataManager.h"
#include "RenderCanvas.h"

using FLIVR::Color;

namespace
{
	QColor ToQColor(const Color& c)
	{
		return QColor::fromRgbF(std::clamp(c.r(), 0.0, 1.0), std::clamp(c.g(), 0.0, 1.0), std::clamp(c.b(), 0.0, 1.0));
	}

	// wx: wxFloatingPointValidator<double>(2) / wxIntegerValidator<unsigned int>
	QLineEdit* MakeText(QWidget* parent, const QString& init, bool integer)
	{
		auto* t = new QLineEdit(init, parent);
		t->setFixedWidth(50);
		if (integer)
			t->setValidator(new QIntValidator(0, 2147483647, t));
		else
		{
			auto* v = new QDoubleValidator(t);
			v->setDecimals(2);
			v->setNotation(QDoubleValidator::StandardNotation);
			v->setLocale(QLocale::c());
			t->setValidator(v);
		}
		return t;
	}

	QSlider* MakeSlider(QWidget* parent, int val, int min, int max)
	{
		auto* s = new QSlider(Qt::Horizontal, parent);
		s->setRange(min, max);
		s->setValue(val);
		s->setMinimumWidth(100);
		return s;
	}

	bool ParseD(QLineEdit* t, double& v)
	{
		bool ok = false;
		double d = t->text().trimmed().toDouble(&ok);
		if (ok) v = d;
		return ok;
	}
}

// ---------------------------------------------------------------------------
// MeshPropPanel (MPropView)
// ---------------------------------------------------------------------------

MeshPropPanel::MeshPropPanel(QWidget* parent)
	: QWidget(parent)
{
	setStyleSheet("QWidget { font-size: 11px; } QLineEdit { min-height: 18px; }");

	auto fixed_label = [this](const QString& t, int w) {
		auto* l = new QLabel(t, this);
		l->setMinimumWidth(w);
		return l;
	};
	auto hrow = [](QBoxLayout* parent_layout, bool indent) {
		auto* h = new QHBoxLayout();
		h->setSpacing(2);
		h->setContentsMargins(0, 0, 0, 0);
		if (indent) h->addSpacing(20);
		if (parent_layout) parent_layout->addLayout(h);
		return h;
	};

	// ---- sizer_1: transparency
	auto* sizer_1 = hrow(nullptr, true);
	sizer_1->addWidget(fixed_label(tr(" Transparency: "), 100));
	m_alpha_sldr = MakeSlider(this, 255, 0, 255);
	m_alpha_text = MakeText(this, "1.00", false);
	sizer_1->addWidget(m_alpha_sldr, 1);
	sizer_1->addWidget(m_alpha_text);

	// ---- sizer_2: shadow
	auto* sizer_2 = hrow(nullptr, true);
	m_shadow_chk = new QCheckBox(tr("Shadow: "), this);
	m_shadow_chk->setFixedWidth(100);
	m_shadow_sldr = MakeSlider(this, 60, 0, 100);
	m_shadow_text = MakeText(this, "0.60", false);
	sizer_2->addWidget(m_shadow_chk);
	sizer_2->addWidget(m_shadow_sldr, 1);
	sizer_2->addWidget(m_shadow_text);

	// ---- sizer_3: lighting
	auto* sizer_3 = hrow(nullptr, true);
	m_light_chk = new QCheckBox(tr("Lighting"), this);
	sizer_3->addWidget(m_light_chk);
	sizer_3->addStretch(1);

	// ---- Material group (sizer_4..6)
	auto* group1 = new QGroupBox(tr("Material"), this);
	auto* g1 = new QVBoxLayout(group1);
	g1->setSpacing(2);
	g1->setContentsMargins(4, 4, 4, 4);
	auto* sizer_4 = hrow(g1, false);
	sizer_4->addWidget(fixed_label(tr(" Diffuse Color: "), 110));
	m_diff_picker = new QPushButton(group1);
	m_diff_picker->setFixedSize(180, 30);
	sizer_4->addWidget(m_diff_picker);
	sizer_4->addStretch(1);
	auto* sizer_5 = hrow(g1, false);
	sizer_5->addWidget(fixed_label(tr(" Specular Color: "), 110));
	m_spec_picker = new QPushButton(group1);
	m_spec_picker->setFixedSize(180, 30);
	sizer_5->addWidget(m_spec_picker);
	sizer_5->addStretch(1);
	auto* sizer_6 = hrow(g1, false);
	sizer_6->addWidget(fixed_label(tr(" Shininess: "), 100));
	m_shine_sldr = MakeSlider(this, 30, 0, 128);
	m_shine_text = MakeText(this, "30", true);
	sizer_6->addWidget(m_shine_sldr, 1);
	sizer_6->addWidget(m_shine_text);
	SetPickerColor(m_diff_picker, Qt::white);
	SetPickerColor(m_spec_picker, Qt::white);

	auto* sizer_v1 = new QVBoxLayout();
	sizer_v1->addWidget(group1);
	sizer_v1->addStretch(1);

	// ---- sizer_7: scaling (hidden in wx)
	m_scale_row = new QWidget(this);
	auto* sizer_7 = new QHBoxLayout(m_scale_row);
	sizer_7->setContentsMargins(0, 0, 0, 0);
	sizer_7->setSpacing(2);
	sizer_7->addSpacing(20);
	sizer_7->addWidget(fixed_label(tr(" Scaling: "), 100));
	m_scale_sldr = MakeSlider(m_scale_row, 100, 50, 200);
	m_scale_text = MakeText(m_scale_row, "1.00", false);
	sizer_7->addWidget(m_scale_sldr, 1);
	sizer_7->addWidget(m_scale_text);

	// ---- sizer_8: size limiter (hidden in wx)
	m_size_row = new QWidget(this);
	auto* sizer_8 = new QHBoxLayout(m_size_row);
	sizer_8->setContentsMargins(0, 0, 0, 0);
	sizer_8->setSpacing(2);
	sizer_8->addSpacing(20);
	m_size_chk = new QCheckBox(tr(" Size limit: "), m_size_row);
	m_size_chk->setFixedWidth(100);
	m_size_sldr = MakeSlider(m_size_row, 50, 0, 250);
	m_size_text = MakeText(m_size_row, "50", true);
	sizer_8->addWidget(m_size_chk);
	sizer_8->addWidget(m_size_sldr, 1);
	sizer_8->addWidget(m_size_text);

	// ---- sizer_9: SWC radius scale
	auto* sizer_9 = hrow(nullptr, true);
	m_r_st = fixed_label(tr(" Radius Scale: "), 100);
	m_r_text = MakeText(this, "1.00", false);
	sizer_9->addWidget(m_r_st);
	sizer_9->addWidget(m_r_text);
	sizer_9->addStretch(1);

	// ---- sizer_11: label
	auto* sizer_11 = hrow(nullptr, true);
	m_label_chk = new QCheckBox(tr("Label"), this);
	m_label_chk->setFixedWidth(100);
	sizer_11->addWidget(m_label_chk);
	sizer_11->addStretch(1);

	// ---- sizer_12: ROI-tree sync (hidden until a tree mesh is shown)
	auto* sizer_12 = hrow(nullptr, false);
	sizer_12->addStretch(1);
	sizer_12->addSpacing(20);
	m_sync_group_chk = new QCheckBox(tr(" Sync (Group)"), this);
	sizer_12->addWidget(m_sync_group_chk);
	sizer_12->addSpacing(20);
	m_sync_name_chk = new QCheckBox(tr(" Sync (Name)"), this);
	sizer_12->addWidget(m_sync_name_chk);
	m_sync_group_chk->hide();
	m_sync_name_chk->hide();

	// ---- sizer_10: sync
	auto* sizer_10 = hrow(nullptr, false);
	sizer_10->addStretch(1);
	sizer_10->addSpacing(20);
	m_sync_chk = new QCheckBox(tr(" Sync"), this);
	sizer_10->addWidget(m_sync_chk);

	auto* sizer_v2 = new QVBoxLayout();
	sizer_v2->setSpacing(2);
	sizer_v2->addSpacing(5);
	sizer_v2->addLayout(sizer_1);
	sizer_v2->addLayout(sizer_2);
	sizer_v2->addLayout(sizer_3);
	sizer_v2->addWidget(m_scale_row);
	sizer_v2->addWidget(m_size_row);
	sizer_v2->addSpacing(5);
	sizer_v2->addLayout(sizer_9);
	sizer_v2->addLayout(sizer_11);
	sizer_v2->addLayout(sizer_12);
	sizer_v2->addLayout(sizer_10);
	sizer_v2->addStretch(1);
	m_scale_row->hide();   // sizer_v2->Hide(sizer_7)
	m_size_row->hide();    // sizer_v2->Hide(sizer_8)

	auto* sizer_all = new QHBoxLayout(this);
	sizer_all->setContentsMargins(4, 2, 4, 2);
	sizer_all->addLayout(sizer_v1, 0);
	sizer_all->addLayout(sizer_v2, 1);

	m_r_st->setEnabled(false);
	m_r_text->setEnabled(false);

	// ---- events (MPropView event table)
	connect(m_light_chk, &QCheckBox::toggled, this, [this]() { if (!m_updating) OnLightingCheck(); });
	connect(m_diff_picker, &QPushButton::clicked, this, &MeshPropPanel::OnDiffChange);
	connect(m_spec_picker, &QPushButton::clicked, this, &MeshPropPanel::OnSpecChange);
	connect(m_shine_sldr, &QSlider::valueChanged, this, &MeshPropPanel::OnShineChange);
	connect(m_alpha_sldr, &QSlider::valueChanged, this, &MeshPropPanel::OnAlphaChange);
	connect(m_scale_sldr, &QSlider::valueChanged, this, &MeshPropPanel::OnScaleChange);
	connect(m_shine_text, &QLineEdit::textEdited, this, [this]() { if (!m_updating) OnShineText(); });
	connect(m_alpha_text, &QLineEdit::textEdited, this, [this]() { if (!m_updating) OnAlphaText(); });
	connect(m_scale_text, &QLineEdit::textEdited, this, [this]() { if (!m_updating) OnScaleText(); });
	connect(m_shadow_chk, &QCheckBox::toggled, this, [this]() { if (!m_updating) OnShadowCheck(); });
	connect(m_shadow_sldr, &QSlider::valueChanged, this, &MeshPropPanel::OnShadowChange);
	connect(m_shadow_text, &QLineEdit::textEdited, this, [this]() { if (!m_updating) OnShadowText(); });
	connect(m_size_chk, &QCheckBox::toggled, this, [this]() { if (!m_updating) OnSizeCheck(); });
	connect(m_size_sldr, &QSlider::valueChanged, this, &MeshPropPanel::OnSizeChange);
	connect(m_size_text, &QLineEdit::textEdited, this, [this]() { if (!m_updating) OnSizeText(); });
	// mpTextCtrl: Enter and focus loss both commit
	connect(m_r_text, &QLineEdit::editingFinished, this, [this]() { if (!m_updating) UpdateRadScale(); });
	connect(m_label_chk, &QCheckBox::toggled, this, [this]() { if (!m_updating) OnLabelCheck(); });
	connect(m_sync_group_chk, &QCheckBox::toggled, this, [this]() { if (!m_updating) OnSyncGroupCheck(); });
	connect(m_sync_name_chk, &QCheckBox::toggled, this, [this]() { if (!m_updating) OnSyncNameCheck(); });
	connect(m_sync_chk, &QCheckBox::toggled, this, [this]() { if (!m_updating) OnSyncCheck(); });

	setEnabled(false);
}

void MeshPropPanel::SetPickerColor(QPushButton* b, const QColor& c)
{
	b->setProperty("pickerColor", c);
	b->setStyleSheet(QStringLiteral("QPushButton { background-color: %1; border: 1px solid #808080; }").arg(c.name()));
}

void MeshPropPanel::SetMeshData(MeshData* md, RenderCanvas* view)
{
	m_md = md;
	m_view = view;
	setEnabled(md != nullptr);
	GetSettings();
}

void MeshPropPanel::GetSettings()
{
	if (!m_md)
		return;

	m_updating = true;
	QSignalBlocker b1(m_shine_sldr), b2(m_alpha_sldr), b3(m_scale_sldr), b4(m_shadow_sldr), b5(m_size_sldr);

	Color amb, diff, spec;
	double shine, alpha;
	m_md->GetMaterial(amb, diff, spec, shine, alpha);

	SetPickerColor(m_diff_picker, ToQColor(diff));
	SetPickerColor(m_spec_picker, ToQColor(spec));

	// lighting
	m_light_chk->setChecked(m_md->GetLighting());
	// shine
	m_shine_sldr->setValue(int(shine));
	m_shine_text->setText(QString::number(shine, 'f', 0));
	// alpha
	m_alpha_sldr->setValue(int(alpha * 255));
	m_alpha_text->setText(QString::number(alpha, 'f', 2));
	// scaling
	double sx, sy, sz;
	m_md->GetScaling(sx, sy, sz);
	m_scale_sldr->setValue(int(sx * 100.0 + 0.5));
	m_scale_text->setText(QString::number(sx, 'f', 2));
	// shadow
	double darkness;
	m_shadow_chk->setChecked(m_md->GetShadow());
	m_md->GetShadowParams(darkness);
	m_shadow_sldr->setValue(int(darkness * 100.0 + 0.5));
	m_shadow_text->setText(QString::number(darkness, 'f', 2));
	// size limiter
	m_size_chk->setChecked(m_md->GetLimit());
	int limit = m_md->GetLimitNumber();
	m_size_sldr->setValue(limit);
	m_size_text->setText(QString::number(limit));
	// label
	if (m_md->isTree())
	{
		m_sync_group_chk->show();
		m_sync_name_chk->show();
		m_sync_chk->hide();
		int id = m_md->GetSelectedSubmeshID();
		if (id >= 0)
		{
			m_label_chk->setEnabled(true);
			m_label_chk->setChecked(m_md->GetSubmeshLabelState(id));
		}
		else
		{
			m_label_chk->setEnabled(false);
			m_label_chk->setChecked(false);
		}
	}
	else
	{
		m_sync_group_chk->hide();
		m_sync_name_chk->hide();
		m_sync_chk->show();
		m_label_chk->setChecked(m_md->GetLabelVisibility());
		m_label_chk->setEnabled(m_md->GetAnnotations() != nullptr);
	}
	// swc
	if (m_md->isSWC())
	{
		m_r_st->setEnabled(true);
		m_r_text->setEnabled(true);
		m_r_text->setText(QString::number(m_md->GetRadScale(), 'f', 2));
	}
	else
	{
		m_r_st->setEnabled(false);
		m_r_text->setEnabled(false);
	}
	m_updating = false;
}

// MPropView::UpdateSync: a mesh in a mesh group syncs every mesh of that
// group; a mesh at the view level syncs the shown meshes that are in no group
void MeshPropPanel::UpdateSync()
{
	if (!m_view || !m_md)
		return;

	Color amb, diff, spec;
	double shine, alpha;
	m_md->GetMaterial(amb, diff, spec, shine, alpha);
	bool lighting = m_md->GetLighting();
	double sx, sy, sz;
	m_md->GetScaling(sx, sy, sz);
	bool shadow = m_md->GetShadow();
	bool limit = m_md->GetLimit();
	int limitnum = m_md->GetLimitNumber();
	double rs = m_md->GetRadScale();

	auto sync = [&](MeshData* md) {
		double dummy1, dummy2;
		md->GetMaterial(amb, diff, spec, dummy1, dummy2);
		md->SetMaterial(amb, diff, spec, shine, alpha);
		md->SetFloat(alpha, MESH_FLOAT_ALPHA);
		md->SetLighting(lighting);
		md->SetScaling(sx, sy, sz);
		md->SetShadow(shadow);
		md->SetLimit(limit);
		md->SetLimitNumer(limitnum);
		md->SetRadScale(rs);
	};

	if (MeshGroup* mg = m_view->GetParentMGroup(m_md))
	{
		for (int i = 0; i < mg->GetMeshNum(); i++)
			if (MeshData* md = mg->GetMeshData(i))
				sync(md);
	}
	else
	{
		// wx m_vrv->GetMeshNum / GetMeshData(i): the shown meshes (PopMeshList)
		for (MeshData* md : m_view->GetPopMeshList())
		{
			if (!md) continue;
			if (m_view->GetParentMGroup(md)) continue;
			sync(md);
		}
	}
}

void MeshPropPanel::OnSyncCheck()
{
	m_sync = m_sync_chk->isChecked();
	if (m_sync)
	{
		UpdateSync();
		Q_EMIT meshChanged();
	}
}

void MeshPropPanel::UpdateRadScale()
{
	double r;
	if (!ParseD(m_r_text, r))
		return;
	if (m_md && r != m_md->GetRadScale())
	{
		m_md->SetRadScale(r);
		if (m_sync) UpdateSync();
		Q_EMIT meshBoundsChanged();
		Q_EMIT meshChanged();
	}
}

// lighting
void MeshPropPanel::OnLightingCheck()
{
	if (m_md)
	{
		m_md->SetLighting(m_light_chk->isChecked());
		if (m_sync) UpdateSync();
		Q_EMIT meshChanged();
	}
}

void MeshPropPanel::OnDiffChange()
{
	if (!m_md) return;
	QColor init = m_diff_picker->property("pickerColor").value<QColor>();
	QColor c = QColorDialog::getColor(init, this, tr("Diffuse color"));
	if (!c.isValid()) return;
	SetPickerColor(m_diff_picker, c);
	Color color(c.red() / 255.0, c.green() / 255.0, c.blue() / 255.0);
	m_md->SetColor(color, MESH_COLOR_DIFF);
	Color amb = color * 0.3;
	m_md->SetColor(amb, MESH_COLOR_AMB);
	Q_EMIT meshChanged();
}

void MeshPropPanel::OnSpecChange()
{
	if (!m_md) return;
	QColor init = m_spec_picker->property("pickerColor").value<QColor>();
	QColor c = QColorDialog::getColor(init, this, tr("Specular color"));
	if (!c.isValid()) return;
	SetPickerColor(m_spec_picker, c);
	Color color(c.red() / 255.0, c.green() / 255.0, c.blue() / 255.0);
	m_md->SetColor(color, MESH_COLOR_SPEC);
	Q_EMIT meshChanged();
}

// slider -> text -> apply, as wx (SetValue on the text fires EVT_TEXT)
void MeshPropPanel::OnShineChange(int v)
{
	m_shine_text->setText(QString::number((double)v, 'f', 0));
	OnShineText();
}

void MeshPropPanel::OnShineText()
{
	double shine;
	if (!ParseD(m_shine_text, shine)) return;
	{ QSignalBlocker b(m_shine_sldr); m_shine_sldr->setValue(int(shine)); }
	if (m_md)
	{
		m_md->SetFloat(shine, MESH_FLOAT_SHN);
		if (m_sync) UpdateSync();
		Q_EMIT meshChanged();
	}
}

void MeshPropPanel::OnAlphaChange(int v)
{
	m_alpha_text->setText(QString::number(v / 255.0, 'f', 2));
	OnAlphaText();
}

void MeshPropPanel::OnAlphaText()
{
	double alpha;
	if (!ParseD(m_alpha_text, alpha)) return;
	{ QSignalBlocker b(m_alpha_sldr); m_alpha_sldr->setValue(int(alpha * 255.0 + 0.5)); }
	if (m_md)
	{
		m_md->SetFloat(alpha, MESH_FLOAT_ALPHA);
		if (m_sync) UpdateSync();
		Q_EMIT meshChanged();
	}
}

void MeshPropPanel::OnScaleChange(int v)
{
	m_scale_text->setText(QString::number(v / 100.0, 'f', 2));
	OnScaleText();
}

void MeshPropPanel::OnScaleText()
{
	double dval;
	if (!ParseD(m_scale_text, dval)) return;
	{ QSignalBlocker b(m_scale_sldr); m_scale_sldr->setValue(int(dval * 100.0 + 0.5)); }
	if (m_md)
	{
		m_md->SetScaling(dval, dval, dval);
		if (m_sync) UpdateSync();
		Q_EMIT meshChanged();
	}
}

// shadow
void MeshPropPanel::OnShadowCheck()
{
	if (m_md)
	{
		m_md->SetShadow(m_shadow_chk->isChecked());
		if (m_sync) UpdateSync();
		Q_EMIT meshChanged();
	}
}

void MeshPropPanel::OnShadowChange(int v)
{
	m_shadow_text->setText(QString::number(v / 100.0, 'f', 2));
	OnShadowText();
}

void MeshPropPanel::OnShadowText()
{
	double dval;
	if (!ParseD(m_shadow_text, dval)) return;
	{ QSignalBlocker b(m_shadow_sldr); m_shadow_sldr->setValue(int(dval * 100.0 + 0.5)); }
	if (m_md)
	{
		m_md->SetShadowParams(dval);
		if (m_sync) UpdateSync();
		Q_EMIT meshChanged();
	}
}

// size limiter
void MeshPropPanel::OnSizeCheck()
{
	if (m_md)
	{
		m_md->SetLimit(m_size_chk->isChecked());
		if (m_sync) UpdateSync();
		Q_EMIT meshChanged();
	}
}

void MeshPropPanel::OnSizeChange(int v)
{
	m_size_text->setText(QString::number(v));
	OnSizeText();
}

void MeshPropPanel::OnSizeText()
{
	bool ok = false;
	long val = m_size_text->text().trimmed().toLong(&ok);
	if (!ok) return;
	{ QSignalBlocker b(m_size_sldr); m_size_sldr->setValue((int)val); }
	if (m_md)
	{
		m_md->SetLimitNumer((int)val);
		if (m_sync) UpdateSync();
		Q_EMIT meshChanged();
	}
}

// label
void MeshPropPanel::OnLabelCheck()
{
	bool bval = m_label_chk->isChecked();
	if (!m_md) return;
	if (m_md->isTree())
	{
		int id = m_md->GetSelectedSubmeshID();
		if (id >= 0)
		{
			m_md->SetSubmeshLabelState(id, bval);
			if (m_group_sync)
				m_md->SetSubmeshLabelStateSiblings(id, m_md->GetSubmeshLabelState(id));
			if (m_name_sync)
				m_md->SetSubmeshLabelStateByName(m_md->GetROIName(id), m_md->GetSubmeshLabelState(id));
		}
	}
	else
		m_md->SetLabelVisibility(bval);
	if (m_sync) UpdateSync();
	Q_EMIT meshChanged();
}

void MeshPropPanel::OnSyncGroupCheck()
{
	m_group_sync = m_sync_group_chk->isChecked();
	if (m_group_sync)
	{
		if (m_md && m_md->isTree())
		{
			int id = m_md->GetSelectedSubmeshID();
			if (id >= 0)
				m_md->SetSubmeshLabelStateSiblings(id, m_md->GetSubmeshLabelState(id));
		}
		Q_EMIT meshChanged();
	}
}

void MeshPropPanel::OnSyncNameCheck()
{
	m_name_sync = m_sync_name_chk->isChecked();
	if (m_name_sync)
	{
		if (m_md && m_md->isTree())
		{
			int id = m_md->GetSelectedSubmeshID();
			if (id >= 0)
				m_md->SetSubmeshLabelStateByName(m_md->GetROIName(id), m_md->GetSubmeshLabelState(id));
		}
		Q_EMIT meshChanged();
	}
}

// ---------------------------------------------------------------------------
// MeshManipPanel (MManipulator)
// ---------------------------------------------------------------------------

MeshManipPanel::Field MeshManipPanel::MakeField(QWidget* row_parent)
{
	Field f;
	f.text = new QLineEdit(row_parent);
	f.text->setFixedWidth(60);
	f.up = new QToolButton(row_parent);
	f.up->setArrowType(Qt::UpArrow);
	f.up->setFixedSize(20, 10);
	f.up->setAutoRepeat(true);
	f.down = new QToolButton(row_parent);
	f.down->setArrowType(Qt::DownArrow);
	f.down->setFixedSize(20, 10);
	f.down->setAutoRepeat(true);
	QLineEdit* t = f.text;
	connect(f.up, &QToolButton::clicked, this, [this, t]() { Step(t, 1.0); });
	connect(f.down, &QToolButton::clicked, this, [this, t]() { Step(t, -1.0); });
	connect(f.text, &QLineEdit::returnPressed, this, [this, t]() { OnValueEnter(t); });
	return f;
}

MeshManipPanel::MeshManipPanel(QWidget* parent)
	: QWidget(parent)
{
	setStyleSheet("QWidget { font-size: 11px; } QLineEdit { min-height: 18px; }");
	auto* sizer_v = new QVBoxLayout(this);
	sizer_v->setSpacing(2);
	sizer_v->setContentsMargins(4, 2, 4, 2);

	auto add_row = [this, sizer_v](const QString& title, Field& x, Field& y, Field& z) {
		sizer_v->addWidget(new QLabel(title, this));
		auto* h = new QHBoxLayout();
		h->setSpacing(2);
		const QString axes[3] = { tr("X:"), tr("Y:"), tr("Z:") };
		Field* fs[3] = { &x, &y, &z };
		for (int i = 0; i < 3; ++i)
		{
			*fs[i] = MakeField(this);
			h->addWidget(new QLabel(axes[i], this));
			h->addWidget(fs[i]->text);
			auto* spin = new QVBoxLayout();
			spin->setSpacing(0);
			spin->setContentsMargins(0, 0, 0, 0);
			spin->addWidget(fs[i]->up);
			spin->addWidget(fs[i]->down);
			h->addLayout(spin);
		}
		h->addStretch(1);
		sizer_v->addLayout(h);
	};
	add_row(tr("Translation:"), m_x_trans, m_y_trans, m_z_trans);
	add_row(tr("Rotation:"), m_x_rot, m_y_rot, m_z_rot);
	add_row(tr("Scaling:"), m_x_scl, m_y_scl, m_z_scl);
	sizer_v->addStretch(1);
	setEnabled(false);
}

void MeshManipPanel::GetData()
{
	if (!m_md)
		return;
	double x, y, z;
	m_md->GetTranslation(x, y, z);
	m_x_trans.text->setText(QString::number(x, 'f', 2));
	m_y_trans.text->setText(QString::number(y, 'f', 2));
	m_z_trans.text->setText(QString::number(z, 'f', 2));
	m_md->GetRotation(x, y, z);
	m_x_rot.text->setText(QString::number(x, 'f', 2));
	m_y_rot.text->setText(QString::number(y, 'f', 2));
	m_z_rot.text->setText(QString::number(z, 'f', 2));
	m_md->GetScaling(x, y, z);
	m_x_scl.text->setText(QString::number(x, 'f', 2));
	m_y_scl.text->setText(QString::number(y, 'f', 2));
	m_z_scl.text->setText(QString::number(z, 'f', 2));
}

// OnSpinUp / OnSpinDown
void MeshManipPanel::Step(QLineEdit* text, double delta)
{
	text->setText(QString::number(text->text().trimmed().toDouble() + delta, 'f', 3));
	UpdateData();
}

void MeshManipPanel::OnValueEnter(QLineEdit* text)
{
	text->setText(QString::number(text->text().trimmed().toDouble(), 'f', 3));
	UpdateData();
}

void MeshManipPanel::UpdateData()
{
	if (!m_md)
		return;
	auto val = [](const Field& f) { return f.text->text().trimmed().toDouble(); };   // STOD: 0 on garbage
	m_md->SetTranslation(val(m_x_trans), val(m_y_trans), val(m_z_trans));
	m_md->SetRotation(val(m_x_rot), val(m_y_rot), val(m_z_rot));
	m_md->SetScaling(val(m_x_scl), val(m_y_scl), val(m_z_scl));
	Q_EMIT meshChanged();
}
