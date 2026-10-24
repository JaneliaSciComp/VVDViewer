#ifndef VVD_QT_ADJUSTPANEL_H
#define VVD_QT_ADJUSTPANEL_H

// "Output Adjustments" panel: Qt port of AdjustView (wx). Per colour channel
// (R,G,B): gamma / luminance / equalization vertical sliders + value boxes,
// a Link checkbox, a Reset button; plus Easy Mode (single "Saturation" level
// slider per channel) and a Set Default button.
//
// Like AdjustView it edits one target, chosen by the selection:
//   SetRenderCanvas  -> the view (wx SetRenderView, m_type 1; editable only
//                       in the Depth blend mode, VOL_METHOD_MULTI)
//   SetVolume        -> a volume (wx SetVolumeData, m_type 2)
//   SetGroup         -> a DataGroup (wx SetGroup, m_type 5)
// and SetGroupLink makes volume edits apply to the whole group (wx
// SetGroupLink). Defaults live in default_2d_adjustment_settings.dft, the
// same file and keys as the wx app.

#include <QSize>
#include <QWidget>

#include "FLIVR/Color.h"

class QCheckBox;
class QLineEdit;
class QPushButton;
class QSlider;
class QWidget;
class RenderCanvas;
class VolumeData;
class DataGroup;
class TreeLayer;

class AdjustPanel : public QWidget
{
	Q_OBJECT
public:
	explicit AdjustPanel(QWidget* parent = nullptr);

	// wx SetRenderView: the canvas becomes the edited target (and, the first
	// time, gets the default view adjustment like the wx frame ctor does).
	void SetRenderCanvas(RenderCanvas* canvas);
	RenderCanvas* GetRenderCanvas() const { return m_canvas; }
	// wx SetVolumeData (nullptr disables the pane, as in wx)
	void SetVolume(VolumeData* vd);
	VolumeData* GetVolume() const { return m_vd; }
	// wx SetGroup / SetGroupLink
	void SetGroup(DataGroup* group);
	void SetGroupLink(DataGroup* group);
	DataGroup* GetGroup() const { return m_group; }
	int GetType() const { return m_type; }   // -1 none, 1 view, 2 volume, 5 group

	// wx GetSettings: re-read the target (e.g. after the blend mode changed)
	void GetSettings();
	// wx UpdateSync: link the channels a multi-channel colour / colormap uses
	void UpdateSync();
	// wx GetDefaults (engine values), for data added later (groups / views)
	void GetDefaults(FLIVR::Color& gamma, FLIVR::Color& brightness, FLIVR::Color& hdr, FLIVR::Color& level,
		bool& sync_r, bool& sync_g, bool& sync_b) const;
	// apply GetDefaults to a newly loaded volume (wx: new volumes inherit them
	// through their group)
	void ApplyDefaults(VolumeData* vd) const;
	void LoadSettings();     // wx LoadSettings (called by the ctor)
	void SaveDefault();      // wx OnSaveDefault ("Set Default")

	// ch: 0=R, 1=G, 2=B. Values are the UI values shown in the boxes.
	double Gamma(int ch) const;          // 0.10 .. 9.99 (1.0 = neutral)
	double Luminance(int ch) const;      // -256 .. 256  (0 = neutral)
	double Equalization(int ch) const;   // 0.0 .. 1.0   (0 = neutral)
	double Level(int ch) const;          // easy-mode saturation, -0.99 .. 0.99
	bool Linked(int ch) const;
	bool EasyMode() const { return m_easy; }

	void SetGamma(int ch, double v);     // programmatic; honours links, applies, emits
	void SetLuminance(int ch, double v);
	void SetEqualization(int ch, double v);
	void SetLevel(int ch, double v);
	void SetLinked(int ch, bool on);     // wx ChangeRSync / OnSyncRCheck
	void ResetAll();

	QSize minimumSizeHint() const override;
	QSize sizeHint() const override;

Q_SIGNALS:
	void adjustmentChanged();            // redraw (wx RefreshVRenderViews)

private Q_SLOTS:
	void onSliderChanged(int ch, int kind, int pos);
	void onTextEdited(int ch, int kind);
	void onLinkToggled(int ch, bool on);
	void onReset(int ch);
	void onSetDefault();
	void onEasyToggled(bool on);

private:
	enum Kind { kGamma = 0, kLum = 1, kEql = 2, kLevel = 3, kKinds = 4 };

	struct Chan
	{
		QCheckBox* link[2] = {nullptr, nullptr};   // default page, easy page
		QSlider* sldr[kKinds] = {};
		QLineEdit* text[kKinds] = {};
		QPushButton* reset[2] = {nullptr, nullptr};
	};

	QWidget* buildPage(bool easy);
	void setValue(int ch, int kind, double v, bool apply);
	void applyValue(int ch, int kind, double ui);
	void refreshWidget(int ch, int kind);
	static double scaleOf(int kind);
	static QString format(int kind, double v);
	static double toEngine(int kind, double ui);
	static double toUi(int kind, double engine);
	TreeLayer* layer() const;
	void setEnabledAll(bool on);
	void showEasyPage(bool on);
	void changeValueEngine(int ch, int kind, double engine);   // wx ChangeXGamma etc.

	RenderCanvas* m_canvas = nullptr;
	VolumeData* m_vd = nullptr;
	DataGroup* m_group = nullptr;
	bool m_link_group = false;
	int m_type = -1;
	bool m_view_defaults_applied = false;
	bool m_easy = false;
	bool m_updating = false;
	bool m_sync[3] = {false, false, false};
	double m_val[3][kKinds];
	// defaults (UI values, wx m_dft_*)
	bool m_use_dft_settings = false;
	double m_dft[3][kKinds];
	bool m_dftSync[3] = {false, false, false};
	Chan m_ch[3];
	QWidget* m_defaultPage = nullptr;
	QWidget* m_easyPage = nullptr;
	QCheckBox* m_easyChk = nullptr;
	QPushButton* m_dftBtn = nullptr;
};

#endif // VVD_QT_ADJUSTPANEL_H
