#include "MeshIO.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTextStream>
#include <QTemporaryFile>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <vector>

#include <glm/glm.hpp>
#include <tinyxml2.h>

#include "DataManager.h"
#include "FLIVR/MeshRenderer.h"
#include "FLIVR/Color.h"
#include "FLIVR/Point.h"

using FLIVR::Color;
using FLIVR::HSVColor;
using FLIVR::Point;

namespace
{
	// wxStringTokenizer(str, ",") in its default (wxTOKEN_RET_EMPTY) mode:
	// empty tokens in the middle are returned, a trailing empty one is not.
	QStringList Tokenize(const QString& str)
	{
		if (str.isEmpty()) return QStringList();
		QStringList t = str.split(QLatin1Char(','));
		if (str.endsWith(QLatin1Char(',')) && !t.isEmpty())
			t.removeLast();
		return t;
	}

	double ToD(const QString& s, double def = 0.0)
	{
		bool ok = false;
		double v = s.trimmed().toDouble(&ok);
		return ok ? v : def;
	}

	// wxFileName::CreateTempFileName(tmpdir/) + ".swc", then wxTextFile write
	QString WriteTempSwc(const QStringList& lines)
	{
		QTemporaryFile tmp(QDir::tempPath() + QStringLiteral("/vvdmeshXXXXXX.swc"));
		tmp.setAutoRemove(false);
		if (!tmp.open()) return QString();
		QTextStream out(&tmp);
		for (const QString& l : lines)
			out << l << '\n';
		out.flush();
		QString path = tmp.fileName();
		tmp.close();
		return path;
	}

	MeshData* LoadOne(DataManager& dm, const QString& path, const QString& desc)
	{
		int before = dm.GetMeshNum();
		if (!dm.LoadMeshData(VVDString(path), VVDString(desc)))
			return nullptr;
		if (dm.GetMeshNum() <= before)
			return nullptr;
		return dm.GetLastMeshData();
	}

	QString FileNameOf(const QString& path)
	{
		return QFileInfo(path).fileName();
	}

	// ---- VRenderFrame::ConvertCSV2SWC ---------------------------------
	MeshData* ConvertCSV2SWC(DataManager& dm, const QString& filename)
	{
		QFile f(filename);
		if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
			return nullptr;
		QStringList csv;
		{
			QTextStream in(&f);
			while (!in.atEnd())
				csv << in.readLine();
		}
		f.close();

		QStringList swcdata;
		std::map<QString, Color> swccolors;
		std::map<QString, FLIVR::MeshRenderer::SubMeshLabel> labels;
		// wx iterates with wxTextFile::Eof(), which stops before the last
		// line; every line is converted here.
		for (int li = 0; li < csv.size(); ++li)
		{
			QStringList elems = Tokenize(csv[li]);
			if (elems.size() >= 10)
			{
				if (swcdata.isEmpty())
				{
					// skip the first line (header)
					++li;
					if (li >= csv.size()) break;
					elems = Tokenize(csv[li]);
					if (elems.size() < 10) continue;
				}
				QString path = QStringLiteral("Cell") + elems[4] + "." + elems[3] + "_" + elems[9];
				double r = 0.1f;
				if (elems.size() >= 15)
				{
					r = ToD(elems[14], r);
					r *= 0.001f;
				}
				swcdata << QStringList({ QString::number(swcdata.size() + 1), "6", elems[1], elems[0], elems[2],
					QString::number(r, 'f', 6), "-1", path }).join(' ');

				double x = ToD(elems[1]), y = ToD(elems[0]), z = ToD(elems[2]);
				Point pos(x, y, z);
				auto it = labels.find(path);
				if (it == labels.end())
				{
					FLIVR::MeshRenderer::SubMeshLabel lbl;
					lbl.name = elems[9].toStdWString();
					lbl.state = false;
					lbl.points.push_back(pos);
					labels[path] = lbl;
				}
				else
					it->second.points.push_back(pos);

				if (elems.size() >= 13)
				{
					Color col(1.0, 1.0, 1.0);
					col[0] = ToD(elems[10], 1.0) / 255.0;
					col[1] = ToD(elems[11], 1.0) / 255.0;
					col[2] = ToD(elems[12], 1.0) / 255.0;
					swccolors[path] = col;

					if (elems.size() >= 14)
					{
						bool ok = false;
						long link2 = elems[13].trimmed().toLong(&ok);
						if (!ok || link2 == 0)
							link2 = -1;
						path = QStringLiteral("Cell") + elems[4] + ".links";
						swcdata << QStringList({ QString::number(swcdata.size() + 1), "0", elems[1], elems[0], elems[2],
							QString::number(r * 0.1, 'f', 6), QString::number((int)link2 * 2), path }).join(' ');
						col[0] = 200.0 / 255.0;
						col[1] = 200.0 / 255.0;
						col[2] = 200.0 / 255.0;
						swccolors[path] = col;
					}
				}
			}
			else if (elems.size() >= 3)
			{
				swcdata << QStringList({ QString::number(swcdata.size() + 1), "6", elems[0], elems[1], elems[2],
					QString::number(0.1f, 'f', 6), "-1" }).join(' ');
			}
		}

		QString temp_swc_path = WriteTempSwc(swcdata);
		if (temp_swc_path.isEmpty())
			return nullptr;

		MeshData* md = LoadOne(dm, temp_swc_path, QString());
		if (!md)
			return nullptr;
		md->SetName(VVDString(FileNameOf(filename)));
		for (auto& kv : labels)
		{
			int id = md->GetROIid(kv.first.toStdWString());
			md->SetSubmeshLabel(id, kv.second);
		}
		for (auto& kv : swccolors)
		{
			const Color& col = kv.second;
			md->SetIDColor((unsigned char)(int)(col.r() * 255), (unsigned char)(int)(col.g() * 255),
				(unsigned char)(int)(col.b() * 255), false, kv.first.toStdWString());
		}
		md->UpdateIDPalette();
		return md;
	}

	// ---- VRenderFrame.cpp parseXML (in-order walk) ----------------------
	bool ParseXML(tinyxml2::XMLDocument& doc, const std::string& search,
		const std::function<void(tinyxml2::XMLNode*)>& found)
	{
		if (doc.ErrorID() != tinyxml2::XML_SUCCESS)
			return false;
		tinyxml2::XMLNode* xElem = doc.FirstChild();
		while (xElem)
		{
			if (xElem->Value() && !std::string(xElem->Value()).compare(search))
				found(xElem);
			if (xElem->FirstChildElement())
				xElem = xElem->FirstChildElement();
			else if (xElem->NextSiblingElement())
				xElem = xElem->NextSiblingElement();
			else
			{
				while (xElem->Parent() && !xElem->Parent()->NextSiblingElement())
					xElem = xElem->Parent();
				if (xElem->Parent() && xElem->Parent()->NextSiblingElement())
					xElem = xElem->Parent()->NextSiblingElement();
				else
					break;
			}
		}
		return true;
	}

	// ---- VRenderFrame::ConvertNML2SWC -----------------------------------
	QList<MeshData*> ConvertNML2SWC(DataManager& dm, const QString& filename)
	{
		QList<MeshData*> result;
		tinyxml2::XMLDocument xmldoc;
		if (xmldoc.LoadFile(QFile::encodeName(filename).constData()) != 0)
			return result;

		glm::vec3 res(1.0f, 1.0f, 1.0f);
		ParseXML(xmldoc, "parameters", [&res](tinyxml2::XMLNode* xElem)
		{
			tinyxml2::XMLElement* elem = xElem->ToElement();
			if (!elem) return;
			tinyxml2::XMLElement* child = elem->FirstChildElement();
			while (child)
			{
				if (strcmp(child->Name(), "scale") == 0)
				{
					if (STOD(child->Attribute("x"))) res.x = STOD(child->Attribute("x"));
					if (STOD(child->Attribute("y"))) res.y = STOD(child->Attribute("y"));
					if (STOD(child->Attribute("z"))) res.z = STOD(child->Attribute("z"));
					break;
				}
				child = child->NextSiblingElement();
			}
		});

		QStringList names;
		std::vector<Color> cols;
		QStringList swcpaths;
		int count = 0;
		bool write_failed = false;
		ParseXML(xmldoc, "thing", [&](tinyxml2::XMLNode* xElem)
		{
			if (write_failed) return;
			tinyxml2::XMLElement* elem = xElem->ToElement();
			if (!elem) return;
			QString name;
			Color col;
			QStringList swclines;
			double radius_scale = glm::length(res);

			if (elem->Attribute("name"))
				name = QString::number(count) + "_" + QString::fromUtf8(elem->Attribute("name"));
			else
				name = QStringLiteral("component_%1").arg(count);
			count++;

			if (elem->Attribute("color.r"))
			{
				col.r(STOD(elem->Attribute("color.r")));
				col.g(STOD(elem->Attribute("color.g")));
				col.b(STOD(elem->Attribute("color.b")));
			}
			else
			{
				double hue = (double)MeshData::NextRandomColorValue() / (RAND_MAX) * 360.0;
				col = Color(HSVColor(hue, 1.0, 1.0));
			}

			std::map<int, glm::vec4> nodes;
			std::map<int, int> nextnode;
			tinyxml2::XMLElement* child = elem->FirstChildElement();
			while (child)
			{
				if (strcmp(child->Name(), "nodes") == 0)
				{
					for (tinyxml2::XMLElement* c2 = child->FirstChildElement(); c2; c2 = c2->NextSiblingElement())
					{
						if (!c2->Name() || strcmp(c2->Name(), "node") != 0) continue;
						int id = STOI(c2->Attribute("id"));
						glm::vec4 v;
						v.x = STOD(c2->Attribute("x")) * res.x;
						v.y = STOD(c2->Attribute("y")) * res.y;
						v.z = STOD(c2->Attribute("z")) * res.z;
						if (c2->Attribute("radius"))
							v.w = STOD(c2->Attribute("radius")) * radius_scale;
						else
							v.w = radius_scale;
						nodes[id] = v;
						if (nextnode.count(id) == 0)
							nextnode[id] = -1;
					}
				}
				else if (strcmp(child->Name(), "edges") == 0)
				{
					for (tinyxml2::XMLElement* c2 = child->FirstChildElement(); c2; c2 = c2->NextSiblingElement())
					{
						if (!c2->Name() || strcmp(c2->Name(), "edge") != 0) continue;
						int s = STOI(c2->Attribute("source"));
						int t = STOI(c2->Attribute("target"));
						nextnode[s] = t;
					}
				}
				child = child->NextSiblingElement();
			}

			for (auto const& x : nodes)
			{
				const glm::vec4& v = x.second;
				swclines << QString::asprintf("%d 0 %f %f %f %f %d", x.first, v.x, v.y, v.z, v.w, nextnode[x.first]);
			}

			QString temp_swc_path = WriteTempSwc(swclines);
			if (temp_swc_path.isEmpty()) { write_failed = true; return; }
			swcpaths << temp_swc_path;
			names << name;
			cols.push_back(col);
		});
		if (write_failed)
			return result;

		if (!swcpaths.isEmpty() && swcpaths.size() == names.size() && swcpaths.size() == (int)cols.size())
		{
			for (int i = 0; i < swcpaths.size(); i++)
			{
				MeshData* md = LoadOne(dm, swcpaths[i], QString());
				if (!md) continue;
				md->SetName(VVDString(names[i]));
				md->SetColor(cols[i], MESH_COLOR_DIFF);
				Color amb = cols[i] * 0.3;
				md->SetColor(amb, MESH_COLOR_AMB);
				result << md;
			}
		}
		return result;
	}
}

QString MeshIO::FileDialogFilter()
{
	return QStringLiteral(
		"All Supported (*.obj *.swc *.ply *.nml);;OBJ files (*.obj);;SWC files (*.swc);;"
		"PLY files (*.ply);;NML files (*.nml)");
}

// VRenderFrame::LoadMeshes (minus the progress dialog and MeshGroup)
bool MeshIO::LoadMeshes(DataManager& dm, const QStringList& files,
	QList<MeshData*>* loaded, const QStringList& descs, QStringList* errors)
{
	bool any = false;
	for (int i = 0; i < files.size(); i++)
	{
		const QString& filename = files[i];
		int dot = filename.lastIndexOf('.');
		QString suffix = dot >= 0 ? filename.mid(dot).toLower() : QString();
		QList<MeshData*> got;
		if (suffix == ".csv")
		{
			if (MeshData* md = ConvertCSV2SWC(dm, filename)) got << md;
		}
		else if (suffix == ".nml")
		{
			got = ConvertNML2SWC(dm, filename);
		}
		else
		{
			if (MeshData* md = LoadOne(dm, filename, i < descs.size() ? descs[i] : QString())) got << md;
		}
		if (got.isEmpty())
		{
			if (errors) *errors << QStringLiteral("Failed to load %1").arg(FileNameOf(filename));
			continue;
		}
		any = true;
		if (loaded) *loaded << got;
	}
	return any;
}
