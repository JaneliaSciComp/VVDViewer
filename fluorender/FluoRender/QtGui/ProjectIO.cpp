// Port of VRenderFrame::SaveProject / OpenProject (VRenderFrame.cpp) for the
// Qt front end. See ProjectIO.h for what is and is not round-tripped.

#include "ProjectIO.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QProgressDialog>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <map>
#include <sstream>

#include "DataManager.h"
#include "Core/FileUtil.h"
#include "VolumePrefetcher.h"
#include "Animator/Interpolator.h"

using vvd::String;

namespace
{
	// ---- value formatting (always C locale: Qt calls setlocale(LC_ALL, "")) --

	String Fmt(double v)   // == printf("%f") / "%lf" in the C locale
	{
		std::wostringstream os;
		os.imbue(std::locale::classic());
		os << std::fixed << std::setprecision(6) << v;
		return String(os.str());
	}

	String Fmt3(double a, double b, double c)
	{
		return Fmt(a) + L" " + Fmt(b) + L" " + Fmt(c);
	}

	String Fmt4(double a, double b, double c, double d)
	{
		return Fmt3(a, b, c) + L" " + Fmt(d);
	}

	String FmtColor(const FLIVR::Color& c) { return Fmt3(c.r(), c.g(), c.b()); }

	// sscanf("%lf%lf%lf") replacement; returns the number of values parsed
	int ParseN(const String& s, double* out, int n)
	{
		std::wistringstream is(static_cast<const std::wstring&>(s));
		is.imbue(std::locale::classic());
		int k = 0;
		while (k < n && (is >> out[k])) ++k;
		return k;
	}

	String Q2S(const QString& s) { return String(s.toStdWString()); }
	QString S2Q(const String& s) { return QString::fromStdWString(s); }

	// ---- a wxFileConfig-like "current path" over vvd::IniFile ---------------

	class Sec
	{
	public:
		Sec(vvd::IniFile& ini, const String& group) : m_ini(ini), m_g(group)
		{
			if (m_g.empty() || m_g[0] != L'/') m_g = String(L"/") + m_g;
			if (m_g.back() != L'/') m_g += L'/';
		}
		Sec Sub(const String& name) const { return Sec(m_ini, m_g + name); }
		String Key(const String& k) const { return m_g + k; }
		const String& Path() const { return m_g; }

		bool Exists() const
		{
			return !m_ini.Keys(m_g).empty() || !m_ini.Groups(m_g).empty();
		}
		bool Has(const String& k) const { return m_ini.Has(Key(k)); }

		bool Read(const String& k, String& v) const { return m_ini.Read(Key(k), v); }
		bool Read(const String& k, double& v) const { return m_ini.Read(Key(k), v); }
		bool Read(const String& k, long& v) const { return m_ini.Read(Key(k), v); }
		bool Read(const String& k, int& v) const { return m_ini.Read(Key(k), v); }
		bool Read(const String& k, bool& v) const { return m_ini.Read(Key(k), v); }
		bool Read(const String& k, QString& v) const
		{
			String s;
			if (!m_ini.Read(Key(k), s)) return false;
			v = S2Q(s);
			return true;
		}
		bool ReadColor(const String& k, FLIVR::Color& c) const
		{
			String s; double v[3];
			if (!Read(k, s) || ParseN(s, v, 3) != 3) return false;
			c = FLIVR::Color(v[0], v[1], v[2]);
			return true;
		}
		bool Read3(const String& k, double& a, double& b, double& c) const
		{
			String s; double v[3];
			if (!Read(k, s) || ParseN(s, v, 3) != 3) return false;
			a = v[0]; b = v[1]; c = v[2];
			return true;
		}
		bool ReadN(const String& k, double* out, int n) const
		{
			String s; double v[4];
			if (n > 4 || !Read(k, s) || ParseN(s, v, n) != n) return false;
			for (int i = 0; i < n; ++i) out[i] = v[i];
			return true;
		}
		long Num() const { long n = 0; Read(L"num", n); return n; }

		void W(const String& k, const String& v) const { m_ini.Write(Key(k), v); }
		void W(const String& k, const wchar_t* v) const { m_ini.Write(Key(k), String(v)); }
		void W(const String& k, const QString& v) const { m_ini.Write(Key(k), Q2S(v)); }
		// wxConfigBase::Write(double) = "%g" in the C locale
		void W(const String& k, double v) const
		{
			std::wostringstream os;
			os.imbue(std::locale::classic());
			os << v;   // default precision 6, no floatfield == %g
			m_ini.Write(Key(k), String(os.str()));
		}
		void W(const String& k, int v) const { m_ini.Write(Key(k), (long)v); }
		void W(const String& k, long v) const { m_ini.Write(Key(k), v); }
		void W(const String& k, bool v) const { m_ini.Write(Key(k), v); }
		void WColor(const String& k, const FLIVR::Color& c) const { W(k, FmtColor(c)); }

	private:
		vvd::IniFile& m_ini;
		String m_g;
	};

	String Idx(const wchar_t* fmt, int i) { return String::Format(fmt, i); }

	// ---- clipping groups (Save/LoadClippingLayerProperties) ------------------

	void WriteClipping(const Sec& s, const ProjectIO::ProjectClipping& c)
	{
		s.W(L"sync_clipping_planes", c.sync_clipping_planes);
		s.W(L"clip_dist_x", c.clip_dist[0]);
		s.W(L"clip_dist_y", c.clip_dist[1]);
		s.W(L"clip_dist_z", c.clip_dist[2]);
		s.W(L"link_x_chk", c.link[0]);
		s.W(L"link_y_chk", c.link[1]);
		s.W(L"link_z_chk", c.link[2]);
		static const wchar_t* lk[6] = { L"linked_x1_param", L"linked_x2_param", L"linked_y1_param",
			L"linked_y2_param", L"linked_z1_param", L"linked_z2_param" };
		for (int i = 0; i < 6; ++i) s.W(lk[i], c.linked_param[i]);
		s.W(L"rotx_cl", c.rot_cl[0]);
		s.W(L"roty_cl", c.rot_cl[1]);
		s.W(L"rotz_cl", c.rot_cl[2]);
		s.W(L"clip_mode", c.clip_mode);
		s.W(L"q_cl_zero", Fmt4(c.q_cl_zero[0], c.q_cl_zero[1], c.q_cl_zero[2], c.q_cl_zero[3]));
		s.W(L"q_cl_fix", Fmt4(c.q_cl_fix[0], c.q_cl_fix[1], c.q_cl_fix[2], c.q_cl_fix[3]));
		s.W(L"q_fix", Fmt4(c.q_fix[0], c.q_fix[1], c.q_fix[2], c.q_fix[3]));
		s.W(L"rotx_cl_fix", c.rot_cl_fix[0]);
		s.W(L"roty_cl_fix", c.rot_cl_fix[1]);
		s.W(L"rotz_cl_fix", c.rot_cl_fix[2]);
		s.W(L"rotx_fix", c.rot_fix[0]);
		s.W(L"roty_fix", c.rot_fix[1]);
		s.W(L"rotz_fix", c.rot_fix[2]);
		s.W(L"trans_fix", Fmt3(c.trans_fix[0], c.trans_fix[1], c.trans_fix[2]));
	}

	// Updates c with the keys present in s (the wx loader's "only if present"
	// rules). Returns whether the group exists.
	bool ReadClipping(const Sec& s, ProjectIO::ProjectClipping& c)
	{
		if (!s.Exists()) return false;
		c.valid = true;
		bool b; int iv; double d;
		if (s.Read(L"sync_clipping_planes", b)) { c.sync_clipping_planes = b; c.has_sync = true; }
		if (s.Read(L"clip_mode", iv)) { c.clip_mode = iv; c.has_mode = true; }
		int dist[3];
		if (s.Read(L"clip_dist_x", dist[0]) && s.Read(L"clip_dist_y", dist[1]) && s.Read(L"clip_dist_z", dist[2]))
		{
			for (int i = 0; i < 3; ++i) c.clip_dist[i] = dist[i];
			c.has_dist = true;
		}
		if (s.Read(L"link_x_chk", b)) c.link[0] = b;
		if (s.Read(L"link_y_chk", b)) c.link[1] = b;
		if (s.Read(L"link_z_chk", b)) c.link[2] = b;
		static const wchar_t* lk[6] = { L"linked_x1_param", L"linked_x2_param", L"linked_y1_param",
			L"linked_y2_param", L"linked_z1_param", L"linked_z2_param" };
		for (int i = 0; i < 6; ++i)
			if (s.Read(lk[i], d)) c.linked_param[i] = d;
		double r[3];
		if (s.Read(L"rotx_cl", r[0]) && s.Read(L"roty_cl", r[1]) && s.Read(L"rotz_cl", r[2]))
		{
			for (int i = 0; i < 3; ++i) c.rot_cl[i] = r[i];
			c.has_rot = true;
		}
		double rcf[3], rf[3];
		if (s.Read(L"rotx_cl_fix", rcf[0]) && s.Read(L"roty_cl_fix", rcf[1]) && s.Read(L"rotz_cl_fix", rcf[2]) &&
			s.Read(L"rotx_fix", rf[0]) && s.Read(L"roty_fix", rf[1]) && s.Read(L"rotz_fix", rf[2]))
		{
			c.has_fix = true;
			for (int i = 0; i < 3; ++i) { c.rot_cl_fix[i] = rcf[i]; c.rot_fix[i] = rf[i]; }
			// wx: a missing quaternion / vector stays default-constructed
			double q[4] = { 0, 0, 0, 1 };
			for (int i = 0; i < 4; ++i) c.q_cl_zero[i] = c.q_cl_fix[i] = c.q_fix[i] = q[i];
			for (int i = 0; i < 3; ++i) c.trans_fix[i] = 0.0;
			s.ReadN(L"q_cl_zero", c.q_cl_zero, 4);
			s.ReadN(L"q_cl_fix", c.q_cl_fix, 4);
			s.ReadN(L"q_fix", c.q_fix, 4);
			s.ReadN(L"trans_fix", c.trans_fix, 3);
		}
		return true;
	}

	// ---- volumes ----------------------------------------------------------------

	int LoadTypeFor(const String& path)
	{
		size_t dot = path.rfind(L'.');
		if (dot == String::npos) return 0;
		String s = String(path.substr(dot)).Lower();
		if (s == L".nrrd") return LOAD_TYPE_NRRD;
		if (s == L".tif" || s == L".tiff" || s == L".zip") return LOAD_TYPE_TIFF;
		if (s == L".oib") return LOAD_TYPE_OIB;
		if (s == L".oif") return LOAD_TYPE_OIF;
		if (s == L".lsm") return LOAD_TYPE_LSM;
		if (s == L".czi") return LOAD_TYPE_CZI;
#ifndef _DARWIN
		if (s == L".nd2") return LOAD_TYPE_ND2;
#endif
		if (s == L".vvd" || s == L".n5" || s == L".json" || s == L".n5fs_ch" || s == L".xml" ||
			s == L".zarr" || s == L".zgroup" || s == L".zfs_ch")
			return LOAD_TYPE_BRKXML;
		if (s == L".h5j") return LOAD_TYPE_H5J;
		if (s == L".v3dpbd") return LOAD_TYPE_V3DPBD;
		if (s == L".idi") return LOAD_TYPE_IDI;
		return 0;
	}

	struct Ctx
	{
		String prj_path;     // the .vrp
		String files_dir;    // <prj>_files
		bool dir_made = false;
		QStringList warnings;

		String FilesDir()
		{
			if (!dir_made) { vvd::Mkdir(files_dir); dir_made = true; }
			return files_dir;
		}
	};

	void SaveVolume(const Sec& s, VolumeData* vd, DataManager* dm, const ProjectIO::SaveOptions& opts, Ctx& ctx)
	{
		const wchar_t sep = vvd::PathSeparator();
		String str;
		s.W(L"name", vd->GetName());
		s.W(L"metadata", vd->GetMetadataPath());
		s.W(L"compression", dm ? dm->GetCompression() : false);
		s.W(L"skip_brick", vd->GetSkipBrick());
		str = vd->GetPath();
		if (str.empty() || opts.embed)
		{
			String ext = vd->GetName().Lower().EndsWith(L".nrrd") ? String() : String(L".nrrd");
			str = ctx.FilesDir() + sep + vd->GetName() + ext;
			vd->Save(str, 2, false, opts.compress);
		}
		s.W(L"path", str);
		if (BaseReader* r = vd->GetReader())
		{
			s.W(L"slice_seq", r->GetSliceSeq());
			s.W(L"time_seq", r->GetTimeSeq());
			s.W(L"time_id", String(r->GetTimeId()));
		}
		else
		{
			s.W(L"slice_seq", false);
			s.W(L"time_id", L"");
		}
		s.W(L"cur_time", vd->GetCurTime());
		s.W(L"cur_chan", opts.embed ? 0 : vd->GetCurChannel());

		Sec p = s.Sub(L"properties");
		p.W(L"display", vd->GetDisp());
		p.W(L"3dgamma", vd->Get3DGamma());
		p.W(L"boundary", vd->GetBoundary());
		p.W(L"contrast", vd->GetOffset());
		p.W(L"left_thresh", vd->GetLeftThresh());
		p.W(L"right_thresh", vd->GetRightThresh());
		p.WColor(L"color", vd->GetColor());
		double hue, sat, val;
		vd->GetHSV(hue, sat, val);
		p.W(L"hsv", Fmt3(hue, sat, val));
		p.WColor(L"mask_color", vd->GetMaskColor());
		p.W(L"mask_color_set", vd->GetMaskColorSet());
		p.W(L"enable_alpha", vd->GetEnableAlpha());
		p.W(L"alpha", vd->GetAlpha());
		double amb, diff, spec, shine;
		vd->GetMaterial(amb, diff, spec, shine);
		p.W(L"ambient", amb);
		p.W(L"diffuse", diff);
		p.W(L"specular", spec);
		p.W(L"shininess", shine);
		p.W(L"shading", vd->GetShading());
		p.W(L"samplerate", vd->GetSampleRate());

		double resx, resy, resz, b_resx, b_resy, b_resz, s_resx, s_resy, s_resz, sclx, scly, sclz;
		vd->GetSpacings(resx, resy, resz);
		vd->GetBaseSpacings(b_resx, b_resy, b_resz);
		vd->GetSpacingScales(s_resx, s_resy, s_resz);
		vd->GetScalings(sclx, scly, sclz);
		p.W(L"res", Fmt3(resx, resy, resz));
		p.W(L"b_res", Fmt3(b_resx, b_resy, b_resz));
		p.W(L"s_res", Fmt3(s_resx, s_resy, s_resz));
		p.W(L"scl", Fmt3(sclx, scly, sclz));

		std::vector<FLIVR::Plane*>* planes = vd->GetVR() ? vd->GetVR()->get_planes() : nullptr;
		if (planes && planes->size() == 6)
		{
			static const wchar_t* pk[6] = { L"x1_param", L"x2_param", L"y1_param", L"y2_param", L"z1_param", L"z2_param" };
			for (int i = 0; i < 6; ++i) p.W(pk[i], (*planes)[i]->GetParam());
		}

		p.WColor(L"gamma", vd->GetGamma());
		p.WColor(L"brightness", vd->GetBrightness());
		p.WColor(L"hdr", vd->GetHdr());
		p.WColor(L"levels", vd->GetLevels());
		p.W(L"sync_r", vd->GetSyncR());
		p.W(L"sync_g", vd->GetSyncG());
		p.W(L"sync_b", vd->GetSyncB());

		p.W(L"colormap_mode", vd->GetColormapMode());
		p.W(L"colormap", vd->GetColormap());
		double low, high;
		vd->GetColormapValues(low, high);
		p.W(L"colormap_lo_value", low);
		p.W(L"colormap_hi_value", high);
		p.W(L"id_color_disp_mode", vd->GetIDColDispMode());

		p.W(L"inv", vd->GetInvert());
		p.W(L"mode", vd->GetMode());
		p.W(L"noise_red", vd->GetNR());
		p.W(L"depth_ovrd", vd->GetBlendMode());
		p.W(L"shadow", vd->GetShadow());
		double shadow_int;
		vd->GetShadowParams(shadow_int);
		p.W(L"shadow_darkness", shadow_int);
		p.W(L"legend", vd->GetLegend());

		std::wstring tree_str = vd->ExportROITree();
		if (!tree_str.empty()) p.W(L"roi_tree", String(tree_str));
		std::string roi_ids = vd->ExportSelIDs();
		if (!roi_ids.empty()) p.W(L"selected_rois", String(roi_ids));
		std::string combined = vd->ExportCombinedROIs();
		if (!combined.empty()) p.W(L"combined_rois", String(combined));
		p.W(L"roi_disp_mode", vd->GetIDColDispMode());

		// mask / label files always go next to the project
		str.clear();
		if (auto mask = vd->GetMask(true))
		{
			str = ctx.FilesDir() + sep + vd->GetName() + L".msk";
			MSKWriter w;
			w.SetData(mask);
			w.SetSpacings(resx, resy, resz);
			w.Save(str.ToStdWstring(), 0);
		}
		p.W(L"mask", str);
		str.clear();
		if (auto label = vd->GetLabel(true))
		{
			str = ctx.FilesDir() + sep + vd->GetName() + L".lbl";
			MSKWriter w;
			w.SetData(label);
			w.SetSpacings(resx, resy, resz);
			w.Save(str.ToStdWstring(), 1);
		}
		p.W(L"label", str);

		p.W(L"mask_disp_mode", vd->GetMaskHideMode());
		p.W(L"mask_lv", vd->GetMaskLv());
		p.W(L"na_mode", vd->GetNAMode());
		p.W(L"shared_mask", vd->GetSharedMaskName());
		p.W(L"shared_label", vd->GetSharedLabelName());
		p.W(L"mask_alpha", vd->GetMaskAlpha());
		p.W(L"max_value", vd->GetMaxValue());   // Qt-only key; ignored by the wx app

		ProjectIO::ProjectClipping c;
		ProjectIO::CaptureClipping(vd, c);
		WriteClipping(p.Sub(L"clipping"), c);
	}

	// Planes from the current (_param) or legacy (_val, _vali) keys; the
	// legacy forms follow VRenderFrame::SetVolumePropertiesFromProject.
	void ReadPlanes(const Sec& p, std::vector<FLIVR::Plane*>* planes, double resx, double resy, double resz)
	{
		if (!planes || planes->size() != 6) return;
		static const wchar_t* names[6] = { L"x1", L"x2", L"y1", L"y2", L"z1", L"z2" };
		const double res[6] = { resx, resx, resy, resy, resz, resz };
		for (int i = 0; i < 6; ++i)
		{
			const bool second = (i % 2) == 1;
			String n(names[i]);
			double v;
			if (p.Read(n + L"_vali", v) && res[i] != 0.0)
			{
				double t = std::fabs(v / res[i]);
				(*planes)[i]->SetParam(second ? 1.0 - t : t);
			}
			else if (p.Read(n + L"_val", v))
			{
				double t = std::fabs(v);
				(*planes)[i]->SetParam(second ? 1.0 - t : t);
			}
			else if (p.Read(n + L"_param", v))
				(*planes)[i]->SetParam(v);
		}
	}

	void ApplyVolumeProperties(const Sec& p, VolumeData* vd, DataManager* dm, long l_major, Ctx& ctx)
	{
		String str;
		bool bval; int ival; double dval;
		if (p.Read(L"display", bval)) vd->SetDisp(bval);

		// old colormap widget
		if (p.Read(L"widget", str))
		{
			std::wistringstream ss(static_cast<const std::wstring&>(str));
			ss.imbue(std::locale::classic());
			std::wstring token;
			int type = 0;
			float left_x = 0, left_y = 0, width = 0, height = 0, offset1 = 0, offset2 = 0, gamma = 1;
			ss >> token >> type >> left_x >> left_y >> width >> height >> offset1 >> offset2 >> gamma;
			if (ss)
			{
				vd->Set3DGamma(gamma);
				vd->SetBoundary(left_y);
				vd->SetOffset(offset1);
				vd->SetLeftThresh(left_x);
				vd->SetRightThresh(left_x + width);
			}
			FLIVR::Color col;
			if (p.ReadColor(L"widgetcolor", col)) vd->SetColor(col);
			if (p.Read(L"widgetalpha", dval)) vd->SetAlpha(dval);
		}

		if (p.Read(L"3dgamma", dval)) vd->Set3DGamma(dval);
		if (p.Read(L"boundary", dval)) vd->SetBoundary(dval);
		if (p.Read(L"contrast", dval)) vd->SetOffset(dval);
		if (p.Read(L"left_thresh", dval)) vd->SetLeftThresh(dval);
		if (p.Read(L"right_thresh", dval)) vd->SetRightThresh(dval);
		FLIVR::Color col;
		if (p.ReadColor(L"color", col)) vd->SetColor(col);
		double h, s, v;
		if (p.Read3(L"hsv", h, s, v)) vd->SetHSV(h, s, v);
		if (p.ReadColor(L"mask_color", col))
		{
			if (p.Read(L"mask_color_set", bval)) vd->SetMaskColor(col, bval);
			else vd->SetMaskColor(col);
		}
		if (p.Read(L"enable_alpha", bval)) vd->SetEnableAlpha(bval);
		if (p.Read(L"alpha", dval)) vd->SetAlpha(dval);

		double amb, diff, spec, shine;
		if (p.Read(L"ambient", amb) && p.Read(L"diffuse", diff) && p.Read(L"specular", spec) && p.Read(L"shininess", shine))
			vd->SetMaterial(amb, diff, spec, shine);
		if (p.Read(L"shading", bval)) vd->SetShading(bval);
		if (p.Read(L"samplerate", dval))
			vd->SetSampleRate(l_major < 2 ? dval / 5.0 : dval);

		double x, y, z;
		if (!vd->isBrxml())
		{
			if (p.Read3(L"res", x, y, z)) vd->SetSpacings(x, y, z);
		}
		else
		{
			if (p.Read3(L"b_res", x, y, z)) vd->SetBaseSpacings(x, y, z);
			if (p.Read3(L"s_res", x, y, z)) vd->SetSpacingScales(x, y, z);
		}
		if (p.Read3(L"scl", x, y, z)) vd->SetScalings(x, y, z);

		int ix = 0, iy = 0, iz = 0;
		vd->GetResolution(ix, iy, iz);
		ReadPlanes(p, vd->GetVR() ? vd->GetVR()->get_planes() : nullptr, ix, iy, iz);

		if (p.ReadColor(L"gamma", col)) vd->SetGamma(col);
		if (p.ReadColor(L"brightness", col)) vd->SetBrightness(col);
		if (p.ReadColor(L"hdr", col)) vd->SetHdr(col);
		if (p.ReadColor(L"levels", col)) vd->SetLevels(col);
		if (p.Read(L"sync_r", bval)) vd->SetSyncR(bval);
		if (p.Read(L"sync_g", bval)) vd->SetSyncG(bval);
		if (p.Read(L"sync_b", bval)) vd->SetSyncB(bval);

		if (p.Read(L"colormap_mode", ival)) vd->SetColormapMode(ival);
		if (p.Read(L"colormap", ival)) vd->SetColormap(ival);
		double low, high;
		if (p.Read(L"colormap_lo_value", low) && p.Read(L"colormap_hi_value", high))
			vd->SetColormapValues(low, high);
		if (p.Read(L"id_color_disp_mode", ival)) vd->SetIDColDispMode(ival);

		if (p.Read(L"inv", bval)) vd->SetInvert(bval);
		if (p.Read(L"mode", ival)) vd->SetMode(ival);
		if (p.Read(L"noise_red", bval)) vd->SetNR(bval);
		if (p.Read(L"depth_ovrd", ival)) vd->SetBlendMode(ival);
		if (p.Read(L"shadow", bval)) vd->SetShadow(bval);
		if (p.Read(L"shadow_darkness", dval)) vd->SetShadowParams(dval);
		if (p.Read(L"legend", bval)) vd->SetLegend(bval);

		if (p.Read(L"roi_tree", str)) vd->ImportROITree(str.ToStdWstring());
		if (p.Read(L"selected_rois", str)) vd->ImportSelIDs(str.ToStdString());
		if (p.Read(L"combined_rois", str)) vd->ImportCombinedROIs(str.ToStdString());
		if (p.Read(L"roi_disp_mode", ival)) vd->SetIDColDispMode(ival);

		if (p.Read(L"mask_lv", ival)) vd->SetMaskLv(ival);

		// mask / label (single-threaded OpenVolumeFromProject path). An empty
		// entry means "no mask saved"; keep whatever the loader found then.
		if (p.Read(L"mask", str) && !str.empty())
		{
			if (!vvd::FileExists(str) && dm) str = dm->SearchProjectPath(str);
			if (vvd::FileExists(str))
			{
				MSKReader reader;
				std::wstring fn = str.ToStdWstring();
				reader.SetFile(fn);
				BaseReader* br = &reader;
				auto mask = br->Convert(true);
				if (vd->GetMask(false)) vd->DeleteMask();
				if (mask) vd->LoadMask(mask);
			}
			else
				ctx.warnings << QString("Mask file not found: %1").arg(S2Q(str));
		}
		if (p.Read(L"label", str) && !str.empty())
		{
			if (!vvd::FileExists(str) && dm) str = dm->SearchProjectPath(str);
			if (vvd::FileExists(str))
			{
				LBLReader reader;
				std::wstring fn = str.ToStdWstring();
				reader.SetFile(fn);
				BaseReader* br = &reader;
				auto label = br->Convert(true);
				if (vd->GetLabel(false)) vd->DeleteLabel();
				if (label) vd->LoadLabel(label);
			}
			else
				ctx.warnings << QString("Label file not found: %1").arg(S2Q(str));
		}

		if (p.Read(L"mask_disp_mode", ival)) vd->SetMaskHideMode(ival);
		if (p.Read(L"na_mode", bval)) vd->SetNAMode(bval);
		if (p.Read(L"shared_mask", str)) vd->SetSharedMaskName(str);
		if (p.Read(L"shared_label", str)) vd->SetSharedLabelName(str);
		if (p.Read(L"mask_alpha", dval)) vd->SetMaskAlpha(dval);
		if (p.Read(L"max_value", dval) && dval > 0.0) vd->SetMaxValue(dval);

		ProjectIO::ProjectClipping c;
		ProjectIO::CaptureClipping(vd, c);
		if (ReadClipping(p.Sub(L"clipping"), c))
			ProjectIO::ApplyClipping(c, vd);
	}

	// The keys of /data/volume/N that LoadVolumeData needs.
	struct VolumeRequest
	{
		bool valid = false;   // has name and path
		String name, path, metadata, time_id;
		bool compression = false, skip_brick = false, slice_seq = false, time_seq = false;
		int cur_chan = 0;
		int cur_time = 0;     // wx: "cur_time" is saved but not restored
		int type = 0;
	};

	VolumeRequest ReadVolumeRequest(const Sec& s)
	{
		VolumeRequest r;
		if (!s.Read(L"name", r.name)) return r;
		if (!s.Read(L"path", r.path)) return r;
		r.valid = true;
		s.Read(L"metadata", r.metadata);
		s.Read(L"compression", r.compression);
		s.Read(L"skip_brick", r.skip_brick);
		if (!s.Read(L"cur_chan", r.cur_chan))
			if (s.Read(L"tiff_chan", r.cur_chan))
				r.cur_chan--;
		s.Read(L"slice_seq", r.slice_seq);
		s.Read(L"time_seq", r.time_seq);
		s.Read(L"time_id", r.time_id);
		r.type = LoadTypeFor(r.path);
		return r;
	}

	// pre: optional VolumePrefetcher result for this request
	VolumeData* LoadVolume(const Sec& s, const VolumeRequest& r, DataManager* dm, long l_major, Ctx& ctx, VolumePrefetch* pre)
	{
		if (!r.valid) return nullptr;
		const String& name = r.name;
		const String& path = r.path;

		dm->SetCompression(r.compression);
		dm->SetSkipBrick(r.skip_brick);
		dm->SetSliceSequence(r.slice_seq);
		dm->SetTimeSequence(r.time_seq);
		dm->SetTimeId(r.time_id);

		int type = r.type;
		if (type == 0)
		{
			ctx.warnings << QString("Unsupported volume type: %1").arg(S2Q(path));
			return nullptr;
		}
		// LoadVolumeData itself falls back to DataManager::SearchProjectPath
		int loaded = dm->LoadVolumeData(path, type, r.cur_chan, r.cur_time, 0, String(), r.metadata, pre);
		VolumeData* vd = loaded > 0 ? dm->GetLastVolumeData() : nullptr;
		if (!vd)
		{
			ctx.warnings << QString("Could not load volume \"%1\" (%2)").arg(S2Q(name), S2Q(path));
			return nullptr;
		}
		vd->SetName(name);
		Sec p = s.Sub(L"properties");
		if (p.Exists())
			ApplyVolumeProperties(p, vd, dm, l_major, ctx);
		return vd;
	}

	// ---- meshes -----------------------------------------------------------------

	void SaveMesh(const Sec& s, MeshData* md, const ProjectIO::SaveOptions& opts, Ctx& ctx)
	{
		const wchar_t sep = vvd::PathSeparator();
		String path = md->GetPath();
		if (path.empty() || opts.embed)
		{
			String lname = md->GetName().Lower();
			if (!md->isSWC())
			{
				String ext = lname.EndsWith(L".obj") ? String() : String(L".obj");
				String dst = ctx.FilesDir() + sep + md->GetName() + ext;
				md->Save(dst);   // also updates md's path
				path = dst;
			}
			else if (!path.empty())
			{
				String ext = lname.EndsWith(L".swc") ? String() : String(L".swc");
				String dst = ctx.FilesDir() + sep + md->GetName() + ext;
				if (vvd::CopyFile(path, dst, true))
					path = dst;   // (the wx app keeps the original path here)
			}
		}
		s.W(L"name", md->GetName());
		s.W(L"path", path);

		Sec p = s.Sub(L"properties");
		p.W(L"display", md->GetDisp());
		p.W(L"lighting", md->GetLighting());
		FLIVR::Color amb, diff, spec;
		double shine, alpha;
		md->GetMaterial(amb, diff, spec, shine, alpha);
		p.WColor(L"ambient", amb);
		p.WColor(L"diffuse", diff);
		p.WColor(L"specular", spec);
		p.W(L"shininess", shine);
		p.W(L"alpha", alpha);
		p.WColor(L"gamma", md->GetGamma());
		p.WColor(L"brightness", md->GetBrightness());
		p.WColor(L"hdr", md->GetHdr());
		p.WColor(L"levels", md->GetLevels());
		p.W(L"sync_r", md->GetSyncR());
		p.W(L"sync_g", md->GetSyncG());
		p.W(L"sync_b", md->GetSyncB());
		p.W(L"shadow", md->GetShadow());
		double darkness;
		md->GetShadowParams(darkness);
		p.W(L"shadow_darkness", darkness);
		p.W(L"radius_scale", md->GetRadScale());
		FLIVR::BBox b = md->GetBounds();
		p.W(L"bound_min", Fmt3(b.min().x(), b.min().y(), b.min().z()));
		p.W(L"bound_max", Fmt3(b.max().x(), b.max().y(), b.max().z()));
		std::vector<FLIVR::Plane*>* planes = md->GetMR() ? md->GetMR()->get_planes() : nullptr;
		if (planes && planes->size() == 6)
		{
			static const wchar_t* pk[6] = { L"x1_param", L"x2_param", L"y1_param", L"y2_param", L"z1_param", L"z2_param" };
			for (int i = 0; i < 6; ++i) p.W(pk[i], (*planes)[i]->GetParam());
		}

		Sec t = s.Sub(L"transform");
		double x, y, z;
		md->GetTranslation(x, y, z);
		t.W(L"translation", Fmt3(x, y, z));
		md->GetRotation(x, y, z);
		t.W(L"rotation", Fmt3(x, y, z));
		md->GetScaling(x, y, z);
		t.W(L"scaling", Fmt3(x, y, z));

		ProjectIO::ProjectClipping c;
		ProjectIO::CaptureClipping(md, c);
		WriteClipping(t.Sub(L"clipping"), c);
	}

	MeshData* LoadMesh(const Sec& s, DataManager* dm, Ctx& ctx)
	{
		String name, path;
		if (!s.Read(L"name", name)) return nullptr;
		if (!s.Read(L"path", path)) return nullptr;
		// LoadMeshData falls back to DataManager::SearchProjectPath
		MeshData* md = dm->LoadMeshData(path) ? dm->GetLastMeshData() : nullptr;
		if (!md)
		{
			ctx.warnings << QString("Could not load mesh \"%1\" (%2)").arg(S2Q(name), S2Q(path));
			return nullptr;
		}
		md->SetName(name);

		Sec p = s.Sub(L"properties");
		if (!p.Exists()) return md;
		bool bval; double dval;
		if (p.Read(L"display", bval)) md->SetDisp(bval);
		if (p.Read(L"lighting", bval)) md->SetLighting(bval);
		// wx: a missing colour keeps the previously parsed one
		double rgb[3] = { 0, 0, 0 };
		p.ReadN(L"ambient", rgb, 3);
		FLIVR::Color amb(rgb[0], rgb[1], rgb[2]);
		p.ReadN(L"diffuse", rgb, 3);
		FLIVR::Color diff(rgb[0], rgb[1], rgb[2]);
		p.ReadN(L"specular", rgb, 3);
		FLIVR::Color spec(rgb[0], rgb[1], rgb[2]);
		double shine = 30.0, alpha = 0.5;
		p.Read(L"shininess", shine);
		p.Read(L"alpha", alpha);
		md->SetMaterial(amb, diff, spec, shine, alpha);
		FLIVR::Color col;
		if (p.ReadColor(L"gamma", col)) md->SetGamma(col);
		if (p.ReadColor(L"brightness", col)) md->SetBrightness(col);
		if (p.ReadColor(L"hdr", col)) md->SetHdr(col);
		if (p.ReadColor(L"levels", col)) md->SetLevels(col);
		if (p.Read(L"sync_r", bval)) md->SetSyncR(bval);   // (wx calls SetSyncG here - a typo)
		if (p.Read(L"sync_g", bval)) md->SetSyncG(bval);
		if (p.Read(L"sync_b", bval)) md->SetSyncB(bval);
		if (p.Read(L"shadow", bval)) md->SetShadow(bval);
		if (p.Read(L"shadow_darkness", dval)) md->SetShadowParams(dval);
		if (p.Read(L"radius_scale", dval)) md->SetRadScale(dval);

		double bmin[3], bmax[3];
		if (p.ReadN(L"bound_min", bmin, 3) && p.ReadN(L"bound_max", bmax, 3))
		{
			FLIVR::BBox bound(FLIVR::Point(bmin[0], bmin[1], bmin[2]), FLIVR::Point(bmax[0], bmax[1], bmax[2]));
			md->SetBounds(bound);
		}
		FLIVR::Vector d = md->GetBounds().diagonal();
		ReadPlanes(p, md->GetMR() ? md->GetMR()->get_planes() : nullptr, (int)d.x(), (int)d.y(), (int)d.z());

		Sec t = s.Sub(L"transform");
		if (t.Exists())
		{
			double x, y, z;
			if (t.Read3(L"translation", x, y, z)) md->SetTranslation(x, y, z);
			if (t.Read3(L"rotation", x, y, z)) md->SetRotation(x, y, z);
			if (t.Read3(L"scaling", x, y, z)) md->SetScaling(x, y, z);
			ProjectIO::ProjectClipping c;
			ProjectIO::CaptureClipping(md, c);
			if (ReadClipping(t.Sub(L"clipping"), c))
				ProjectIO::ApplyClipping(c, md);
		}
		return md;
	}

	// ---- view layers ----------------------------------------------------------

	void WriteLayer(const Sec& l, const ProjectIO::ProjectLayer& layer)
	{
		l.W(L"type", layer.type);
		l.W(L"name", layer.name);
		if (layer.type != ProjectIO::ProjectLayer::VolumeGroup && layer.type != ProjectIO::ProjectLayer::MeshGroup)
			return;
		l.W(L"id", layer.id);
		l.W(L"display", layer.display);
		if (layer.type == ProjectIO::ProjectLayer::VolumeGroup)
		{
			l.WColor(L"gamma", layer.gamma);
			l.WColor(L"brightness", layer.brightness);
			l.WColor(L"hdr", layer.hdr);
			l.WColor(L"levels", layer.levels);
			l.W(L"sync_r", layer.sync_r);
			l.W(L"sync_g", layer.sync_g);
			l.W(L"sync_b", layer.sync_b);
			l.W(L"sync_vp", layer.sync_vp);
			l.W(L"sync_vspc", layer.sync_vspc);
			Sec v = l.Sub(L"volumes");
			v.W(L"num", (int)layer.members.size());
			for (int k = 0; k < (int)layer.members.size(); ++k)
				v.W(Idx(L"vol_%d", k), layer.members[k]);
			WriteClipping(v.Sub(L"clipping"), layer.clipping);
		}
		else
		{
			l.W(L"sync_mp", layer.sync_mp);
			Sec m = l.Sub(L"meshes");
			m.W(L"num", (int)layer.members.size());
			for (int k = 0; k < (int)layer.members.size(); ++k)
				m.W(Idx(L"mesh_%d", k), layer.members[k]);
			WriteClipping(m.Sub(L"clipping"), layer.clipping);
		}
	}

	bool ReadLayer(const Sec& l, ProjectIO::ProjectLayer& layer)
	{
		int type;
		if (!l.Read(L"type", type)) return false;
		layer.type = type;
		if (!l.Read(L"name", layer.name)) return false;
		if (type != ProjectIO::ProjectLayer::VolumeGroup && type != ProjectIO::ProjectLayer::MeshGroup)
			return true;
		l.Read(L"id", layer.id);
		l.Read(L"display", layer.display);
		if (type == ProjectIO::ProjectLayer::VolumeGroup)
		{
			l.ReadColor(L"gamma", layer.gamma);
			l.ReadColor(L"brightness", layer.brightness);
			l.ReadColor(L"hdr", layer.hdr);
			l.ReadColor(L"levels", layer.levels);
			l.Read(L"sync_r", layer.sync_r);
			l.Read(L"sync_g", layer.sync_g);
			l.Read(L"sync_b", layer.sync_b);
			l.Read(L"sync_vp", layer.sync_vp);
			l.Read(L"sync_vspc", layer.sync_vspc);
			Sec v = l.Sub(L"volumes");
			long n = v.Num();
			for (long k = 0; k < n; ++k)
			{
				QString m;
				if (v.Read(Idx(L"vol_%d", (int)k), m)) layer.members.push_back(m);
			}
			ReadClipping(v.Sub(L"clipping"), layer.clipping);
		}
		else
		{
			l.Read(L"sync_mp", layer.sync_mp);
			Sec m = l.Sub(L"meshes");
			long n = m.Num();
			for (long k = 0; k < n; ++k)
			{
				QString s;
				if (m.Read(Idx(L"mesh_%d", (int)k), s)) layer.members.push_back(s);
			}
			ReadClipping(m.Sub(L"clipping"), layer.clipping);
		}
		return true;
	}

	// ---- view properties --------------------------------------------------------

	void WriteViewProperties(const Sec& p, const ProjectIO::ProjectView& v)
	{
		p.W(L"drawall", v.draw_all);
		p.W(L"persp", v.persp);
		p.W(L"free", v.free);
		p.W(L"aov", v.aov);
		p.W(L"min_dpi", v.min_dpi);
		p.W(L"res_mode", v.res_mode);
		p.W(L"nearclip", v.near_clip);
		p.W(L"farclip", v.far_clip);
		p.WColor(L"backgroundcolor", v.bg_color);
		p.W(L"drawtype", v.draw_type);
		p.W(L"volmethod", v.vol_method);
		p.W(L"peellayers", v.peel_layers);
		p.W(L"fog", v.fog);
		p.W(L"fogintensity", v.fog_intensity);
		p.W(L"draw_camctr", v.draw_camctr);
		p.W(L"draw_fps", v.draw_fps);
		p.W(L"draw_legend", v.draw_legend);

		p.W(L"translation", Fmt3(v.transx, v.transy, v.transz));
		p.W(L"rotation", Fmt3(v.rotx, v.roty, v.rotz));
		if (v.has_camera_frame)
		{
			p.W(L"center", Fmt3(v.ctrx, v.ctry, v.ctrz));
			p.W(L"centereyedist", v.center_eye_dist);
			p.W(L"radius", v.radius);
			p.W(L"initdist", v.init_dist);
		}
		p.W(L"scale", v.scale_factor);
		p.W(L"obj_center", Fmt3(v.obj_ctrx, v.obj_ctry, v.obj_ctrz));
		p.W(L"obj_trans", Fmt3(v.obj_transx, v.obj_transy, v.obj_transz));
		p.W(L"obj_rot", Fmt3(v.obj_rotx, v.obj_roty, v.obj_rotz));

		p.W(L"disp_scale_bar", v.disp_scale_bar);
		p.W(L"disp_scale_bar_text", v.disp_scale_bar_text);
		p.W(L"sb_length", v.sb_length);
		p.W(L"sb_text", v.sb_text);
		p.W(L"sb_num", v.sb_num);
		p.W(L"sb_unit", v.sb_unit);
		p.W(L"sb_digit", v.sb_digit);
		p.W(L"sb_fixed", v.sb_fixed);

		p.WColor(L"gamma", v.gamma);
		p.WColor(L"brightness", v.brightness);
		p.WColor(L"hdr", v.hdr);
		p.WColor(L"levels", v.levels);
		p.W(L"sync_r", v.sync_r);
		p.W(L"sync_g", v.sync_g);
		p.W(L"sync_b", v.sync_b);
		p.W(L"easy_2d_adjustment", v.easy_2d_adjustment);

		p.W(L"st_time", v.st_time);
		p.W(L"ed_time", v.ed_time);
		p.W(L"cur_time", v.cur_time);

		WriteClipping(p, v.clipping);
	}

	void ReadViewProperties(const Sec& p, ProjectIO::ProjectView& v)
	{
		p.Read(L"drawall", v.draw_all);
		if (!p.Read(L"persp", v.persp)) v.persp = true;    // wx defaults for old files
		if (!p.Read(L"free", v.free)) v.free = false;
		p.Read(L"aov", v.aov);
		p.Read(L"min_dpi", v.min_dpi);
		p.Read(L"res_mode", v.res_mode);
		p.Read(L"nearclip", v.near_clip);
		p.Read(L"farclip", v.far_clip);
		p.ReadColor(L"backgroundcolor", v.bg_color);
		p.Read(L"drawtype", v.draw_type);
		p.Read(L"volmethod", v.vol_method);
		p.Read(L"peellayers", v.peel_layers);
		p.Read(L"fog", v.fog);
		p.Read(L"fogintensity", v.fog_intensity);
		p.Read(L"draw_camctr", v.draw_camctr);
		p.Read(L"draw_fps", v.draw_fps);
		v.has_draw_legend = p.Read(L"draw_legend", v.draw_legend);

		p.Read3(L"translation", v.transx, v.transy, v.transz);
		p.Read3(L"rotation", v.rotx, v.roty, v.rotz);
		if (p.Read3(L"center", v.ctrx, v.ctry, v.ctrz)) v.has_camera_frame = true;
		double dist = 0.0;
		bool has_dist = p.Read(L"centereyedist", dist);
		if (has_dist) { v.center_eye_dist = dist; v.has_camera_frame = true; }
		double radius = 5.0;
		if (p.Read(L"radius", radius)) { v.radius = radius; v.has_camera_frame = true; }
		const double half_aov = v.aov / 2.0 * 3.14159265358979323846 / 180.0;
		const double tan_half = std::tan(half_aov);
		if (!p.Read(L"initdist", v.init_dist) && tan_half != 0.0)
			v.init_dist = radius / tan_half;
		if (!p.Read(L"scale", v.scale_factor) && has_dist && dist != 0.0 && tan_half != 0.0)
			v.scale_factor = radius / tan_half / dist;
		p.Read3(L"obj_center", v.obj_ctrx, v.obj_ctry, v.obj_ctrz);
		p.Read3(L"obj_trans", v.obj_transx, v.obj_transy, v.obj_transz);
		p.Read3(L"obj_rot", v.obj_rotx, v.obj_roty, v.obj_rotz);

		p.Read(L"disp_scale_bar", v.disp_scale_bar);
		p.Read(L"disp_scale_bar_text", v.disp_scale_bar_text);
		p.Read(L"sb_length", v.sb_length);
		p.Read(L"sb_text", v.sb_text);
		p.Read(L"sb_num", v.sb_num);
		p.Read(L"sb_unit", v.sb_unit);
		p.Read(L"sb_digit", v.sb_digit);
		p.Read(L"sb_fixed", v.sb_fixed);

		p.ReadColor(L"gamma", v.gamma);
		p.ReadColor(L"brightness", v.brightness);
		p.ReadColor(L"hdr", v.hdr);
		p.ReadColor(L"levels", v.levels);
		p.Read(L"sync_r", v.sync_r);
		p.Read(L"sync_g", v.sync_g);
		p.Read(L"sync_b", v.sync_b);
		p.Read(L"easy_2d_adjustment", v.easy_2d_adjustment);

		p.Read(L"st_time", v.st_time);
		p.Read(L"ed_time", v.ed_time);
		p.Read(L"cur_time", v.cur_time);

		ReadClipping(p, v.clipping);
	}

	// ---- rulers (VRenderFrame::SaveProject / OpenProject, /views/N/rulers) ----

	void WriteRulers(const Sec& rs, const std::vector<Ruler*>& ruler_list)
	{
		if (ruler_list.empty())
			return;
		rs.W(L"num", (long)ruler_list.size());
		for (size_t ri = 0; ri < ruler_list.size(); ++ri)
		{
			Ruler* ruler = ruler_list[ri];
			if (!ruler) continue;
			Sec r = rs.Sub(Idx(L"%d", (int)ri));
			r.W(L"name", ruler->GetName());
			r.W(L"name_disp", ruler->GetNameDisp());
			r.W(L"description", ruler->GetDesc());
			r.W(L"type", ruler->GetRulerType());
			r.W(L"display", ruler->GetDisp());
			r.W(L"transient", ruler->GetTimeDep());
			r.W(L"time", ruler->GetTime());
			r.W(L"info_names", ruler->GetInfoNames());
			r.W(L"info_values", ruler->GetInfoValues());
			r.W(L"use_color", ruler->GetUseColor());
			r.WColor(L"color", ruler->GetColor());
			r.W(L"num", ruler->GetNumPoint());
			for (int rpi = 0; rpi < ruler->GetNumPoint(); ++rpi)
			{
				FLIVR::Point* rp = ruler->GetPoint(rpi);
				if (!rp) continue;
				Sec rps = r.Sub(L"points").Sub(Idx(L"%d", rpi));
				rps.W(L"point", Fmt3(rp->x(), rp->y(), rp->z()));
				// name of the volume the point was picked on (Qt only; the
				// wx reader ignores the key)
				String pvol = ruler->GetPointVolName(rpi);
				if (!pvol.empty())
					rps.W(L"volume", pvol);
			}
		}
	}

	void ReadRulers(const Sec& rs, std::vector<Ruler*>& ruler_list)
	{
		if (!rs.Exists())
			return;
		long rnum = rs.Num();
		for (int ri = 0; ri < rnum; ++ri)
		{
			Sec r = rs.Sub(Idx(L"%d", ri));
			if (!r.Exists())
				continue;
			Ruler* ruler = new Ruler();
			String str;
			int iVal;
			bool bVal;
			if (r.Read(L"name", str))
				ruler->SetName(str);
			if (r.Read(L"name_disp", str))
				ruler->SetNameDisp(str);
			if (r.Read(L"description", str))
				ruler->SetDesc(str);
			if (r.Read(L"type", iVal))
				ruler->SetRulerType(iVal);
			if (r.Read(L"display", bVal))
				ruler->SetDisp(bVal);
			if (r.Read(L"transient", bVal))
				ruler->SetTimeDep(bVal);
			if (r.Read(L"time", iVal))
				ruler->SetTime(iVal);
			if (r.Read(L"info_names", str))
				ruler->SetInfoNames(str);
			if (r.Read(L"info_values", str))
				ruler->SetInfoValues(str);
			if (r.Read(L"use_color", bVal))
			{
				if (bVal)
				{
					FLIVR::Color col;
					if (r.ReadColor(L"color", col))
						ruler->SetColor(col);
				}
			}
			long pnum = r.Num();
			for (int rpi = 0; rpi < pnum; ++rpi)
			{
				Sec pt = r.Sub(L"points").Sub(Idx(L"%d", rpi));
				if (!pt.Exists())
					continue;
				double x, y, z;
				if (pt.Read3(L"point", x, y, z))
				{
					FLIVR::Point point(x, y, z);
					if (ruler->AddPoint(point) && pt.Read(L"volume", str))
						ruler->SetPointVolName(ruler->GetNumPoint() - 1, str);
				}
			}
			ruler_list.push_back(ruler);
		}
	}

	const wchar_t* kRulersPrefix = L"/views/0/rulers/";
	const wchar_t* kInterpolatorPrefix = L"/interpolator/";

	// ---- /interpolator (wx VRenderFrame::SaveProject / OpenProject) ----------
	// wx writes no value for FLKEY_TYPE_INT keys (and skips them on load);
	// their value is written here as well ("val"), which wx ignores.

	void WriteInterpolator(const Sec& s, Interpolator& ip)
	{
		s.W(L"max_id", Interpolator::m_id);
		int group_num = ip.GetKeyNum();
		s.W(L"num", group_num);
		for (int i = 0; i < group_num; i++)
		{
			FlKeyGroup* key_group = ip.GetKeyGroup(i);
			if (!key_group) continue;
			Sec g = s.Sub(Idx(L"%d", i));
			g.W(L"id", key_group->id);
			g.W(L"t", key_group->t);
			g.W(L"type", key_group->type);
			g.W(L"desc", String(key_group->desc));
			int key_num = (int)key_group->keys.size();
			Sec ks = g.Sub(L"keys");
			ks.W(L"num", key_num);
			for (int j = 0; j < key_num; j++)
			{
				FlKey* key = key_group->keys[j];
				if (!key) continue;
				Sec kk = ks.Sub(Idx(L"%d", j));
				int key_type = key->GetType();
				kk.W(L"type", key_type);
				FLKeyCode code = key->GetKeyCode();
				kk.W(L"l0", code.l0);
				kk.W(L"l0_name", String(code.l0_name));
				kk.W(L"l1", code.l1);
				kk.W(L"l1_name", String(code.l1_name));
				kk.W(L"l2", code.l2);
				kk.W(L"l2_name", String(code.l2_name));
				switch (key_type)
				{
				case FLKEY_TYPE_DOUBLE:
					kk.W(L"val", ((FlKeyDouble*)key)->GetValue());
					break;
				case FLKEY_TYPE_QUATER:
				{
					FLIVR::Quaternion qval = ((FlKeyQuaternion*)key)->GetValue();
					kk.W(L"val", Fmt4(qval.x, qval.y, qval.z, qval.w));
				}
				break;
				case FLKEY_TYPE_BOOLEAN:
					kk.W(L"val", ((FlKeyBoolean*)key)->GetValue());
					break;
				case FLKEY_TYPE_INT:
					kk.W(L"val", ((FlKeyInt*)key)->GetValue());
					break;
				}
			}
		}
	}

	void ReadInterpolator(const Sec& s, Interpolator& ip)
	{
		ip.Clear();
		int iVal;
		if (s.Read(L"max_id", iVal))
			Interpolator::m_id = iVal;
		std::vector<FlKeyGroup*>* key_list = ip.GetKeyList();
		long group_num = s.Num();
		for (int i = 0; i < group_num; i++)
		{
			Sec g = s.Sub(Idx(L"%d", i));
			if (!g.Exists()) continue;
			FlKeyGroup* key_group = new FlKeyGroup;
			key_group->id = 0;
			key_group->t = 0.0;
			key_group->type = 0;
			double dVal;
			String sVal;
			if (g.Read(L"id", iVal)) key_group->id = iVal;
			if (g.Read(L"t", dVal)) key_group->t = dVal;
			if (g.Read(L"type", iVal)) key_group->type = iVal;
			if (g.Read(L"desc", sVal)) key_group->desc = sVal.ToStdString();
			Sec ks = g.Sub(L"keys");
			if (ks.Exists())
			{
				long key_num = ks.Num();
				for (int j = 0; j < key_num; j++)
				{
					Sec kk = ks.Sub(Idx(L"%d", j));
					if (!kk.Exists()) continue;
					int key_type;
					if (!kk.Read(L"type", key_type)) continue;
					FLKeyCode code;
					code.l0 = code.l1 = code.l2 = 0;
					if (kk.Read(L"l0", iVal)) code.l0 = iVal;
					if (kk.Read(L"l0_name", sVal)) code.l0_name = sVal.ToStdString();
					if (kk.Read(L"l1", iVal)) code.l1 = iVal;
					if (kk.Read(L"l1_name", sVal)) code.l1_name = sVal.ToStdString();
					if (kk.Read(L"l2", iVal)) code.l2 = iVal;
					if (kk.Read(L"l2_name", sVal)) code.l2_name = sVal.ToStdString();
					switch (key_type)
					{
					case FLKEY_TYPE_DOUBLE:
						if (kk.Read(L"val", dVal))
							key_group->keys.push_back(new FlKeyDouble(code, dVal));
						break;
					case FLKEY_TYPE_QUATER:
					{
						double v[4];
						if (kk.Read(L"val", sVal) && ParseN(sVal, v, 4) == 4)
						{
							FLIVR::Quaternion qval(v[0], v[1], v[2], v[3]);
							key_group->keys.push_back(new FlKeyQuaternion(code, qval));
						}
					}
					break;
					case FLKEY_TYPE_BOOLEAN:
					{
						bool bVal;
						if (kk.Read(L"val", bVal))
							key_group->keys.push_back(new FlKeyBoolean(code, bVal));
					}
					break;
					case FLKEY_TYPE_INT:
						if (kk.Read(L"val", iVal))
							key_group->keys.push_back(new FlKeyInt(code, iVal));
						break;
					}
				}
			}
			key_list->push_back(key_group);
		}
	}

	// keys of these sections are kept verbatim between load and save
	const wchar_t* kPassthrough[] = {
		L"/views/0/properties/",   // unmodelled keys only (brush_*, w2d, ...)
		L"/views/0/rulers/",
		L"/interpolator/",
		L"/brush_diag/",
		L"/ui_layout/",
		L"/memory settings/",
	};

	void CollectKeys(const vvd::IniFile& ini, const String& group, std::vector<std::pair<QString, QString>>& out)
	{
		for (const String& k : ini.Keys(group))
		{
			String full = group + k, v;
			if (ini.Read(full, v)) out.emplace_back(S2Q(full), S2Q(v));
		}
		for (const String& g : ini.Groups(group))
			CollectKeys(ini, group + g + L"/", out);
	}
}

namespace ProjectIO
{
	void CaptureClipping(ClippingLayer* layer, ProjectClipping& c)
	{
		if (!layer) return;
		c.valid = true;
		c.sync_clipping_planes = layer->GetSyncClippingPlanes();
		layer->GetClipDistance(c.clip_dist[0], c.clip_dist[1], c.clip_dist[2]);
		c.link[0] = layer->GetClippingLinkX();
		c.link[1] = layer->GetClippingLinkY();
		c.link[2] = layer->GetClippingLinkZ();
		for (int i = 0; i < 6; ++i) c.linked_param[i] = layer->GetLinkedParam(i);
		layer->GetClippingPlaneRotationsRaw(c.rot_cl[0], c.rot_cl[1], c.rot_cl[2]);
		FLIVR::Quaternion q_cl_zero, q_cl_fix, q_fix;
		FLIVR::Vector trans_fix;
		layer->GetClippingFixParams(c.clip_mode, q_cl_zero, q_cl_fix, q_fix,
			c.rot_cl_fix[0], c.rot_cl_fix[1], c.rot_cl_fix[2],
			c.rot_fix[0], c.rot_fix[1], c.rot_fix[2], trans_fix);
		auto q4 = [](const FLIVR::Quaternion& q, double* o) { o[0] = q.x; o[1] = q.y; o[2] = q.z; o[3] = q.w; };
		q4(q_cl_zero, c.q_cl_zero);
		q4(q_cl_fix, c.q_cl_fix);
		q4(q_fix, c.q_fix);
		c.trans_fix[0] = trans_fix.x(); c.trans_fix[1] = trans_fix.y(); c.trans_fix[2] = trans_fix.z();
		c.has_fix = true;
	}

	void ApplyClipping(const ProjectClipping& c, ClippingLayer* layer)
	{
		if (!layer || !c.valid) return;
		layer->SetSyncClippingPlanes(c.sync_clipping_planes);
		layer->SetClipDistance(c.clip_dist[0], c.clip_dist[1], c.clip_dist[2]);
		layer->SetClippingLinkX(c.link[0]);
		layer->SetClippingLinkY(c.link[1]);
		layer->SetClippingLinkZ(c.link[2]);
		for (int i = 0; i < 6; ++i) layer->SetLinkedParam(i, c.linked_param[i]);
		layer->SetClippingPlaneRotationsRaw(c.rot_cl[0], c.rot_cl[1], c.rot_cl[2]);
		if (c.has_fix)
		{
			auto q4 = [](const double* o) { return FLIVR::Quaternion(o[0], o[1], o[2], o[3]); };
			layer->SetClippingFixParams(c.clip_mode, q4(c.q_cl_zero), q4(c.q_cl_fix), q4(c.q_fix),
				c.rot_cl_fix[0], c.rot_cl_fix[1], c.rot_cl_fix[2],
				c.rot_fix[0], c.rot_fix[1], c.rot_fix[2],
				FLIVR::Vector(c.trans_fix[0], c.trans_fix[1], c.trans_fix[2]));
		}
	}

	bool SaveProject(const QString& path, DataManager* dm,
		const std::vector<VolumeData*>& volumes,
		const std::vector<MeshData*>& meshes,
		const ProjectView& view, QString* error,
		const SaveOptions& opts)
	{
		if (path.isEmpty())
		{
			if (error) *error = QStringLiteral("No project file name given");
			return false;
		}
		vvd::IniFile ini;
		Ctx ctx;
		ctx.prj_path = Q2S(path);
		ctx.files_dir = ctx.prj_path + L"_files";

		Sec root(ini, L"/");
		root.W(L"ver_major", String(VERSION_MAJOR_TAG));
		root.W(L"ver_minor", String(VERSION_MINOR_TAG));
		root.W(L"ticks", (int)(volumes.size() + meshes.size()));

		// wx SaveProject: the SettingDlg streaming settings
		if (view.memory.valid)
		{
			Sec ms(ini, L"/memory settings");
			ms.W(L"mem swap", view.memory.mem_swap);
			ms.W(L"graphics mem", view.memory.graphics_mem);
			ms.W(L"large data size", view.memory.large_data_size);
			ms.W(L"force brick size", view.memory.force_brick_size);
			ms.W(L"up time", view.memory.up_time);
			ms.W(L"update order", view.memory.update_order);
		}

		// data list: only non-null entries get an index
		std::vector<VolumeData*> vols;
		for (VolumeData* vd : volumes) if (vd) vols.push_back(vd);
		std::vector<MeshData*> mshs;
		for (MeshData* md : meshes) if (md) mshs.push_back(md);

		Sec dv(ini, L"/data/volume");
		dv.W(L"num", (int)vols.size());
		for (int i = 0; i < (int)vols.size(); ++i)
			SaveVolume(dv.Sub(Idx(L"%d", i)), vols[i], dm, opts, ctx);

		Sec dmsh(ini, L"/data/mesh");
		dmsh.W(L"num", (int)mshs.size());
		for (int i = 0; i < (int)mshs.size(); ++i)
			SaveMesh(dmsh.Sub(Idx(L"%d", i)), mshs[i], opts, ctx);

		// wx SaveProject /data/annotations (DataManager order)
		{
			Sec da(ini, L"/data/annotations");
			const int na = dm ? dm->GetAnnotationNum() : 0;
			da.W(L"num", na);
			for (int i = 0; i < na; ++i)
			{
				Annotations* ann = dm->GetAnnotations(i);
				if (!ann) continue;
				if (ann->GetPath().empty())
					ann->Save(ctx.FilesDir() + vvd::PathSeparator() + ann->GetName() + L".txt");
				Sec a = da.Sub(Idx(L"%d", i));
				a.W(L"name", ann->GetName());
				a.W(L"path", ann->GetPath());
				if (MeshData* md = ann->GetMesh())
				{
					FLIVR::Color amb, diff, spec;
					double shine, alpha;
					md->GetMaterial(amb, diff, spec, shine, alpha);
					a.WColor(L"color", diff);
					a.W(L"transparency", ann->GetAlpha());
					a.W(L"max_score", ann->GetMaxScore());
					a.W(L"threshold", ann->GetThreshold());
				}
			}
		}

		// views (wx: /views/num, then /views/<i>/layers, properties, rulers)
		Sec views(ini, L"/views");
		views.W(L"num", 1 + (int)view.extra_views.size());
		std::vector<ProjectLayer> layers = view.layers;
		if (layers.empty())
		{
			for (VolumeData* vd : vols) { ProjectLayer l; l.type = ProjectLayer::Volume; l.name = S2Q(vd->GetName()); layers.push_back(l); }
			for (MeshData* md : mshs) { ProjectLayer l; l.type = ProjectLayer::Mesh; l.name = S2Q(md->GetName()); layers.push_back(l); }
		}
		Sec ls(ini, L"/views/0/layers");
		ls.W(L"num", (int)layers.size());
		for (int j = 0; j < (int)layers.size(); ++j)
			WriteLayer(ls.Sub(Idx(L"%d", j)), layers[j]);
		WriteViewProperties(Sec(ini, L"/views/0/properties"), view);
		if (view.rulers)
			WriteRulers(Sec(ini, L"/views/0/rulers"), *view.rulers);
		for (int vi = 0; vi < (int)view.extra_views.size(); ++vi)
		{
			const ProjectView& ev = view.extra_views[vi];
			const String base = Idx(L"/views/%d", vi + 1);
			Sec els(ini, base + L"/layers");
			els.W(L"num", (int)ev.layers.size());
			for (int j = 0; j < (int)ev.layers.size(); ++j)
				WriteLayer(els.Sub(Idx(L"%d", j)), ev.layers[j]);
			WriteViewProperties(Sec(ini, base + L"/properties"), ev);
			if (ev.rulers)
				WriteRulers(Sec(ini, base + L"/rulers"), *ev.rulers);
			// the view's unmodelled /views/<i>/properties keys
			for (const auto& kv : ev.passthrough)
			{
				String k = Q2S(kv.first);
				if (!ini.Has(k)) ini.Write(k, Q2S(kv.second));
			}
		}
		if (view.interpolator)
			WriteInterpolator(Sec(ini, L"/interpolator"), *view.interpolator);

		// prop panel: selection indices refer to /data/volume/N, /data/mesh/N
		int sel_vol = -1, sel_mesh = -1, sel_type = -1;
		for (int i = 0; i < (int)vols.size(); ++i) if (vols[i] == view.selected_volume) sel_vol = i;
		for (int i = 0; i < (int)mshs.size(); ++i) if (mshs[i] == view.selected_mesh) sel_mesh = i;
		if (sel_vol >= 0) sel_type = 2;
		else if (sel_mesh >= 0) sel_type = 3;
		if (view.raw_selection)
		{
			sel_type = view.cur_sel_type;
			sel_vol = view.cur_sel_vol;
			sel_mesh = view.cur_sel_mesh;
		}
		Sec pp(ini, L"/prop_panel");
		pp.W(L"cur_sel_type", sel_type);
		pp.W(L"cur_sel_vol", sel_vol);
		pp.W(L"cur_sel_mesh", sel_mesh);
		pp.W(L"plane_mode", view.plane_mode);

		const ProjectMovie& mv = view.movie;
		Sec m(ini, L"/movie_panel");
		m.W(L"views_cmb", mv.views_cmb);
		m.W(L"rot_check", mv.rot_check);
		m.W(L"seq_check", mv.seq_check);
		m.W(L"frame_range", mv.frame_range);
		m.W(L"time_frame", mv.time_frame);
		m.W(L"x_rd", mv.x_rd);
		m.W(L"y_rd", mv.y_rd);
		m.W(L"z_rd", mv.z_rd);
		m.W(L"angle_end_text", mv.angle_end_text);
		m.W(L"step_text", mv.step_text);
		m.W(L"frames_text", mv.frames_text);
		m.W(L"frame_chk", mv.frame_chk);
		m.W(L"center_x_text", mv.center_x_text);
		m.W(L"center_y_text", mv.center_y_text);
		m.W(L"width_text", mv.width_text);
		m.W(L"height_text", mv.height_text);
		m.W(L"time_start_text", mv.time_start_text);
		m.W(L"time_cur_text", mv.time_cur_text);
		m.W(L"time_end_text", mv.time_end_text);
		m.W(L"progress_text", mv.progress_text);

		Sec(ini, L"/tree_panel").W(L"exp_states", view.tree_exp_states);

		// sections the Qt app does not model, as loaded (never overriding the above)
		const QString rulers_prefix = S2Q(String(kRulersPrefix));
		for (const auto& kv : view.passthrough)
		{
			if (view.rulers && kv.first.startsWith(rulers_prefix))
				continue;   // written from the ruler list above
			if (view.interpolator && kv.first.startsWith(S2Q(String(kInterpolatorPrefix))))
				continue;   // written from the interpolator above
			String k = Q2S(kv.first);
			if (!ini.Has(k)) ini.Write(k, Q2S(kv.second));
		}

		if (!ini.Save(ctx.prj_path))
		{
			if (error) *error = QString("Could not write %1").arg(path);
			return false;
		}
		if (error) *error = ctx.warnings.join('\n');
		return true;
	}

	bool LoadProject(const QString& path, DataManager* dm,
		std::vector<VolumeData*>& volumes_out,
		std::vector<MeshData*>& meshes_out,
		ProjectView& view, QString* error, QWidget* progress_parent)
	{
		String prj = Q2S(path);
		vvd::IniFile ini;
		if (!dm || !vvd::FileExists(prj) || !ini.Load(prj))
		{
			if (error) *error = QString("Could not read project file %1").arg(path);
			return false;
		}
		Ctx ctx;
		ctx.prj_path = prj;
		dm->SetProjectPath(prj);

		// version
		Sec root(ini, L"/");
		String s_major, s_minor;
		long l_major = 1, l_minor = 0;
		view.ver_major = view.ver_minor = 0;
		view.newer_version = false;
		view.selected_volume = nullptr;
		view.selected_mesh = nullptr;
		if (root.Read(L"ver_major", s_major) && root.Read(L"ver_minor", s_minor))
		{
			s_major.ToLong(&l_major);
			s_minor.ToLong(&l_minor);
			view.ver_major = l_major;
			view.ver_minor = l_minor;
			// wx OpenProject: major newer, else minor newer (whatever the major)
			view.newer_version = l_major > VERSION_MAJOR || l_minor > VERSION_MINOR;
		}

		// volumes (by file index, null when a volume failed to load).
		// wx VRenderFrame::OpenProject -> OpenVolumesFromProjectMT: the files
		// are read and decoded by ProjectDataLoader threads; each volume is
		// then finished here, in file order (see VolumePrefetcher).
		QElapsedTimer timer;
		timer.start();
		const bool timing = qEnvironmentVariableIsSet("VVD_QT_LOAD_TIMING");
		Sec dv(ini, L"/data/volume");
		Sec dmsh(ini, L"/data/mesh");
		long nv = dv.Num();
		long nm = dmsh.Num();
		std::vector<VolumeRequest> vreq((size_t)std::max(0L, nv));
		std::vector<int> pre_index(vreq.size(), -1);
		std::vector<VolumePrefetcher::Request> prefetch;
		for (long i = 0; i < nv; ++i)
		{
			Sec s = dv.Sub(Idx(L"%d", (int)i));
			if (!s.Exists()) continue;
			vreq[i] = ReadVolumeRequest(s);
			if (!vreq[i].valid || vreq[i].type == 0) continue;
			VolumePrefetcher::Request q;
			q.filename = vreq[i].path;
			q.type = vreq[i].type;
			q.ch_num = vreq[i].cur_chan;
			q.t_num = vreq[i].cur_time;
			q.slice_seq = vreq[i].slice_seq;
			q.time_seq = vreq[i].time_seq;
			q.time_id = vreq[i].time_id;
			pre_index[i] = (int)prefetch.size();
			prefetch.push_back(q);
		}

		std::unique_ptr<QProgressDialog> prg;
		if (progress_parent && nv + nm > 0)
		{
			prg = std::make_unique<QProgressDialog>(QString("Reading project file. Please wait."), QString(), 0, (int)(nv + nm), progress_parent);
			prg->setWindowTitle(QString("Open Project"));
			prg->setWindowModality(Qt::ApplicationModal);
			prg->setCancelButton(nullptr);
			prg->setMinimumDuration(300);
			prg->setAutoClose(false);
			prg->setAutoReset(false);
			prg->setValue(0);
		}
		int finished = 0;
		QElapsedTimer idle_timer;
		idle_timer.start();
		VolumePrefetcher prefetcher(dm, prefetch);
		// keep the UI alive while waiting (wx: wxMilliSleep + progress Update)
		auto idle = [&]() {
			if (idle_timer.elapsed() < 50) return;
			idle_timer.restart();
			if (prg)
			{
				// decoded files count half, finished volumes the other half
				const int v = (prefetcher.Decoded() + finished) / 2;
				prg->setValue(std::min(v, (int)(nv + nm)));
			}
			QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
		};

		std::vector<VolumeData*> vol_by_idx;
		qint64 wait_ms = 0, finish_ms = 0;
		for (long i = 0; i < nv; ++i)
		{
			Sec s = dv.Sub(Idx(L"%d", (int)i));
			std::unique_ptr<VolumePrefetch> pre;
			QElapsedTimer t_wait;
			t_wait.start();
			if (pre_index[i] >= 0)
				pre = prefetcher.Wait((size_t)pre_index[i], idle);
			wait_ms += t_wait.restart();
			vol_by_idx.push_back(s.Exists() ? LoadVolume(s, vreq[i], dm, l_major, ctx, pre.get()) : nullptr);
			finish_ms += t_wait.elapsed();
			++finished;
			idle();
		}
		if (timing)
			fprintf(stderr, "[load] project volumes: %ld in %lld ms (%d threads; main thread: %lld ms waiting for decode, %lld ms finishing)\n",
				nv, (long long)timer.elapsed(), prefetcher.Threads(), (long long)wait_ms, (long long)finish_ms);

		std::vector<MeshData*> mesh_by_idx;
		for (long i = 0; i < nm; ++i)
		{
			Sec s = dmsh.Sub(Idx(L"%d", (int)i));
			mesh_by_idx.push_back(s.Exists() ? LoadMesh(s, dm, ctx) : nullptr);
			if (prg) prg->setValue((int)(nv + i + 1));
		}
		prg.reset();

		// wx OpenProject /data/annotations: DataManager::LoadAnnotations (the
		// .txt + .lbl), the mesh of the annotations gets the saved colour
		{
			Sec da(ini, L"/data/annotations");
			for (long i = 0, n = da.Num(); i < n; ++i)
			{
				Sec a = da.Sub(Idx(L"%d", (int)i));
				if (!a.Exists()) continue;
				String apath;
				Annotations* ann = nullptr;
				if (a.Read(L"path", apath))
					ann = dm->LoadAnnotations(apath);
				if (!ann)
				{
					ctx.warnings << QString("Could not load annotations %1").arg(S2Q(apath));
					continue;
				}
				if (MeshData* md = ann->GetMesh())
				{
					FLIVR::Color color(FLIVR::HSVColor(0.0, 0.0, 1.0));
					md->SetColor(color, MESH_COLOR_DIFF);
					FLIVR::Color amb = color * 0.3;
					md->SetColor(amb, MESH_COLOR_AMB);
					FLIVR::Color col;
					if (a.ReadColor(L"color", col))
					{
						md->SetColor(col, MESH_COLOR_DIFF);
						amb = col * 0.3;
						md->SetColor(amb, MESH_COLOR_AMB);
					}
					dm->AddMeshData(md);
					ann->SetMesh(md);
					double d;
					if (a.Read(L"transparency", d)) ann->SetAlpha(d);
					if (a.Read(L"max_score", d)) ann->SetMaxScore(d);
					if (a.Read(L"threshold", d)) ann->SetThreshold(d);
				}
			}
		}

		// view 0 (further views: below)
		Sec v0(ini, L"/views/0");
		if (v0.Exists())
		{
			view.layers.clear();
			// old format: plain name lists
			Sec ov = v0.Sub(L"volumes");
			for (long j = 0, n = ov.Num(); j < n; ++j)
			{
				ProjectLayer l; l.type = ProjectLayer::Volume;
				if (ov.Read(Idx(L"name%d", (int)j), l.name)) view.layers.push_back(l);
			}
			Sec om = v0.Sub(L"meshes");
			for (long j = 0, n = om.Num(); j < n; ++j)
			{
				ProjectLayer l; l.type = ProjectLayer::Mesh;
				if (om.Read(Idx(L"name%d", (int)j), l.name)) view.layers.push_back(l);
			}
			// new format: layer tree
			Sec ls = v0.Sub(L"layers");
			for (long j = 0, n = ls.Num(); j < n; ++j)
			{
				Sec l = ls.Sub(Idx(L"%d", (int)j));
				if (!l.Exists()) continue;
				ProjectLayer layer;
				if (ReadLayer(l, layer)) view.layers.push_back(layer);
			}
			Sec p = v0.Sub(L"properties");
			if (p.Exists()) ReadViewProperties(p, view);

			// rulers: wx clears the view's list, then reads /views/N/rulers
			if (view.rulers)
			{
				for (Ruler* r : *view.rulers) delete r;
				view.rulers->clear();
				ReadRulers(v0.Sub(L"rulers"), *view.rulers);
			}
		}
		// wx OpenProject: /views/<i> for i = 1 .. num-1 (CreateView each); the
		// same keys as view 0. Defaults: the members of a fresh ProjectView
		// pre-filled from view 0 by the caller are not available here, so
		// each further view starts from the pre-filled extra_views[i-1] when
		// the caller provided one, else from a default ProjectView.
		{
			const long vnum = Sec(ini, L"/views").Num();
			std::vector<ProjectView> extras;
			for (long vi = 1; vi < vnum; ++vi)
			{
				ProjectView ev;
				if (vi - 1 < (long)view.extra_views.size())
				{
					ev = view.extra_views[vi - 1];
					ev.extra_views.clear();
				}
				ev.passthrough.clear();
				ev.owned_rulers.clear();
				ev.rulers = nullptr;
				const String base = Idx(L"/views/%d", (int)vi);
				Sec vs(ini, base);
				ev.layers.clear();
				Sec ov = vs.Sub(L"volumes");
				for (long j = 0, n = ov.Num(); j < n; ++j)
				{
					ProjectLayer l; l.type = ProjectLayer::Volume;
					if (ov.Read(Idx(L"name%d", (int)j), l.name)) ev.layers.push_back(l);
				}
				Sec om = vs.Sub(L"meshes");
				for (long j = 0, n = om.Num(); j < n; ++j)
				{
					ProjectLayer l; l.type = ProjectLayer::Mesh;
					if (om.Read(Idx(L"name%d", (int)j), l.name)) ev.layers.push_back(l);
				}
				Sec els = vs.Sub(L"layers");
				for (long j = 0, n = els.Num(); j < n; ++j)
				{
					Sec l = els.Sub(Idx(L"%d", (int)j));
					if (!l.Exists()) continue;
					ProjectLayer layer;
					if (ReadLayer(l, layer)) ev.layers.push_back(layer);
				}
				Sec p = vs.Sub(L"properties");
				if (p.Exists()) ReadViewProperties(p, ev);
				ReadRulers(vs.Sub(L"rulers"), ev.owned_rulers);
				CollectKeys(ini, base + L"/properties/", ev.passthrough);
				extras.push_back(std::move(ev));
			}
			view.extra_views = std::move(extras);
		}

		// output lists: view layer order first, then the rest in file order
		std::map<String, VolumeData*> vol_by_name;
		for (VolumeData* vd : vol_by_idx) if (vd) vol_by_name.emplace(vd->GetName(), vd);
		std::map<String, MeshData*> mesh_by_name;
		for (MeshData* md : mesh_by_idx) if (md) mesh_by_name.emplace(md->GetName(), md);
		volumes_out.clear();
		meshes_out.clear();
		auto add_vol = [&](const QString& n) {
			auto it = vol_by_name.find(Q2S(n));
			if (it != vol_by_name.end() && std::find(volumes_out.begin(), volumes_out.end(), it->second) == volumes_out.end())
				volumes_out.push_back(it->second);
		};
		auto add_mesh = [&](const QString& n) {
			auto it = mesh_by_name.find(Q2S(n));
			if (it != mesh_by_name.end() && std::find(meshes_out.begin(), meshes_out.end(), it->second) == meshes_out.end())
				meshes_out.push_back(it->second);
		};
		for (const ProjectLayer& l : view.layers)
		{
			switch (l.type)
			{
			case ProjectLayer::Volume: add_vol(l.name); break;
			case ProjectLayer::Mesh: add_mesh(l.name); break;
			case ProjectLayer::VolumeGroup: for (const QString& n : l.members) add_vol(n); break;
			case ProjectLayer::MeshGroup: for (const QString& n : l.members) add_mesh(n); break;
			default: break;
			}
		}
		// then the layers of the further views, in view order
		const size_t v0_vols = volumes_out.size(), v0_meshes = meshes_out.size();
		for (const ProjectView& ev : view.extra_views)
			for (const ProjectLayer& l : ev.layers)
			{
				switch (l.type)
				{
				case ProjectLayer::Volume: add_vol(l.name); break;
				case ProjectLayer::Mesh: add_mesh(l.name); break;
				case ProjectLayer::VolumeGroup: for (const QString& n : l.members) add_vol(n); break;
				case ProjectLayer::MeshGroup: for (const QString& n : l.members) add_mesh(n); break;
				default: break;
				}
			}
		std::vector<VolumeData*> extra_vols(volumes_out.begin() + v0_vols, volumes_out.end());
		std::vector<MeshData*> extra_meshes(meshes_out.begin() + v0_meshes, meshes_out.end());
		for (VolumeData* vd : vol_by_idx)
			if (vd && std::find(volumes_out.begin(), volumes_out.end(), vd) == volumes_out.end())
				volumes_out.push_back(vd);
		for (MeshData* md : mesh_by_idx)
			if (md && std::find(meshes_out.begin(), meshes_out.end(), md) == meshes_out.end())
				meshes_out.push_back(md);

		// wx: draw_legend == false switches every legend of the view off
		// (view 0: everything not in a further view)
		if (v0.Sub(L"properties").Has(L"draw_legend") && !view.draw_legend)
		{
			for (VolumeData* vd : volumes_out)
				if (std::find(extra_vols.begin(), extra_vols.end(), vd) == extra_vols.end()) vd->SetLegend(false);
			for (MeshData* md : meshes_out)
				if (std::find(extra_meshes.begin(), extra_meshes.end(), md) == extra_meshes.end()) md->SetLegend(false);
		}
		for (int vi = 0; vi < (int)view.extra_views.size(); ++vi)
		{
			const ProjectView& ev = view.extra_views[vi];
			if (!Sec(ini, Idx(L"/views/%d/properties", vi + 1)).Has(L"draw_legend") || ev.draw_legend)
				continue;
			auto in_view = [&ev](const String& name) {
				for (const ProjectLayer& l : ev.layers)
				{
					if (Q2S(l.name) == name) return true;
					for (const QString& m : l.members) if (Q2S(m) == name) return true;
				}
				return false;
			};
			for (VolumeData* vd : volumes_out) if (in_view(vd->GetName())) vd->SetLegend(false);
			for (MeshData* md : meshes_out) if (in_view(md->GetName())) md->SetLegend(false);
		}

		// selection / clip panel
		Sec pp(ini, L"/prop_panel");
		if (pp.Exists())
		{
			int type = -1, sv = -1, sm = -1;
			bool has_type = pp.Read(L"cur_sel_type", type);
			bool has_sv = pp.Read(L"cur_sel_vol", sv);
			bool has_sm = pp.Read(L"cur_sel_mesh", sm);
			view.has_selection = has_type && has_sv && has_sm;
			view.cur_sel_type = has_type ? type : -1;
			view.cur_sel_vol = has_sv ? sv : -1;
			view.cur_sel_mesh = has_sm ? sm : -1;
			if ((type == 2 || !has_type) && sv >= 0 && sv < (int)vol_by_idx.size())
				view.selected_volume = vol_by_idx[sv];
			else if (type == 3 && sm >= 0 && sm < (int)mesh_by_idx.size())
				view.selected_mesh = mesh_by_idx[sm];
			view.has_plane_mode = pp.Read(L"plane_mode", view.plane_mode);
			// legacy keys (old projects; wx reads them, never writes them)
			view.has_chann_link = pp.Read(L"chann_link", view.chann_link);
			static const wchar_t* ll[6] = { L"linked_x1", L"linked_x2", L"linked_y1", L"linked_y2", L"linked_z1", L"linked_z2" };
			view.has_linked_legacy = pp.Has(L"linked_x1");
			for (int k = 0; k < 6; ++k) pp.Read(ll[k], view.linked_legacy[k]);
			static const wchar_t* al[3] = { L"x_link", L"y_link", L"z_link" };
			for (int k = 0; k < 3; ++k)
			{
				bool b;
				view.has_axis_link[k] = pp.Read(al[k], b) ? (b ? 1 : 0) : -1;
			}
		}

		{
			Sec ms(ini, L"/memory settings");
			if (ms.Exists())
			{
				ProjectView::Memory& mem = view.memory;
				mem.valid = true;
				ms.Read(L"mem swap", mem.mem_swap);
				ms.Read(L"graphics mem", mem.graphics_mem);
				ms.Read(L"large data size", mem.large_data_size);
				ms.Read(L"force brick size", mem.force_brick_size);
				ms.Read(L"up time", mem.up_time);
				ms.Read(L"update order", mem.update_order);
			}
		}

		Sec m(ini, L"/movie_panel");
		if (m.Exists())
		{
			ProjectMovie& mv = view.movie;
			mv.valid = true;
			m.Read(L"views_cmb", mv.views_cmb);
			m.Read(L"rot_check", mv.rot_check);
			m.Read(L"seq_check", mv.seq_check);
			m.Read(L"frame_range", mv.frame_range);
			m.Read(L"time_frame", mv.time_frame);
			m.Read(L"x_rd", mv.x_rd);
			m.Read(L"y_rd", mv.y_rd);
			m.Read(L"z_rd", mv.z_rd);
			m.Read(L"angle_end_text", mv.angle_end_text);
			m.Read(L"step_text", mv.step_text);
			m.Read(L"frames_text", mv.frames_text);
			m.Read(L"frame_chk", mv.frame_chk);
			m.Read(L"center_x_text", mv.center_x_text);
			m.Read(L"center_y_text", mv.center_y_text);
			m.Read(L"width_text", mv.width_text);
			m.Read(L"height_text", mv.height_text);
			m.Read(L"time_start_text", mv.time_start_text);
			m.Read(L"time_cur_text", mv.time_cur_text);
			m.Read(L"time_end_text", mv.time_end_text);
			m.Read(L"progress_text", mv.progress_text);
		}

		Sec(ini, L"/tree_panel").Read(L"exp_states", view.tree_exp_states);

		// wx OpenProject: the interpolator is replaced only when the file has keys
		if (view.interpolator)
		{
			Sec ip(ini, L"/interpolator");
			if (ip.Exists())
			{
				ReadInterpolator(ip, *view.interpolator);
				view.interpolator_loaded = true;
			}
		}

		view.passthrough.clear();
		for (const wchar_t* prefix : kPassthrough)
		{
			if (view.rulers && String(prefix) == String(kRulersPrefix))
				continue;   // loaded into view.rulers
			if (view.interpolator && String(prefix) == String(kInterpolatorPrefix))
				continue;   // loaded into view.interpolator
			CollectKeys(ini, String(prefix), view.passthrough);
		}

		if (error) *error = ctx.warnings.join('\n');
		return true;
	}
}
