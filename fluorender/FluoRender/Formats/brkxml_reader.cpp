#include "brkxml_reader.h"
#include "FLIVR/TextureRenderer.h"
#include "FLIVR/ShaderProgram.h"
#include "FLIVR/Utils.h"
//#include "FLIVR/Point.h"
#include "../compatibility.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <locale>
#include <algorithm>
#include <regex>
#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include "boost/filesystem.hpp"

using namespace boost::filesystem;

using nlohmann::json;

template <typename _T> void clear2DVector(std::vector<std::vector<_T>> &vec2d)
{
	if(vec2d.empty())return;
	for(int i = 0; i < vec2d.size(); i++)std::vector<_T>().swap(vec2d[i]);
	std::vector<std::vector<_T>>().swap(vec2d);
}

template <typename _T> inline void SafeDelete(_T* &p)
{
	if(p != NULL){
		delete (p);
		(p) = NULL;
	}
}

BRKXMLReader::BRKXMLReader()
{
   m_resize_type = 0;
   m_resample_type = 0;
   m_alignment = 0;

   m_level_num = 0;
   m_cur_level = -1;

   m_time_num = 0;
   m_cur_time = -1;
   m_chan_num = 0;
   m_cur_chan = 0;
   m_slice_num = 0;
   m_x_size = 0;
   m_y_size = 0;
   
   m_valid_spc = false;
   m_xspc = 0.0;
   m_yspc = 0.0;
   m_zspc = 0.0;

   m_max_value = 0.0;
   m_scalar_scale = 1.0;

   m_batch = false;
   m_cur_batch = -1;
   m_file_type = BRICK_FILE_TYPE_NONE;

   m_ex_metadata_path = wstring();
   m_ex_metadata_url = wstring();
   
   m_isURL = false;

   m_copy_lv = -1;
   m_mask_lv = -1;
}

BRKXMLReader::~BRKXMLReader()
{
	Clear();
}

void BRKXMLReader::Clear()
{
	if(m_pyramid.empty()) return;
	for(int i = 0; i < m_pyramid.size(); i++){
		if(!m_pyramid[i].bricks.empty()){
			for(int j = 0; j < m_pyramid[i].bricks.size(); j++) SafeDelete(m_pyramid[i].bricks[j]);
			vector<BrickInfo *>().swap(m_pyramid[i].bricks);
		}
		if(!m_pyramid[i].filename.empty()){
			for(int j = 0; j < m_pyramid[i].filename.size(); j++){
				if(!m_pyramid[i].filename[j].empty()){
					for(int k = 0; k < m_pyramid[i].filename[j].size(); k++){
						if(!m_pyramid[i].filename[j][k].empty()){
							for(int m = 0; m < m_pyramid[i].filename[j][k].size(); m++)
								SafeDelete(m_pyramid[i].filename[j][k][m]);
							vector<FLIVR::FileLocInfo *>().swap(m_pyramid[i].filename[j][k]);
						}
					}
					vector<vector<FLIVR::FileLocInfo *>>().swap(m_pyramid[i].filename[j]);
				}
			}
			vector<vector<vector<FLIVR::FileLocInfo *>>>().swap(m_pyramid[i].filename);
		}
	}
	vector<LevelInfo>().swap(m_pyramid);

	vector<Landmark>().swap(m_landmarks);
}

//Use Before Preprocess()
void BRKXMLReader::SetFile(string &file)
{
   if (!file.empty())
   {
      if (!m_path_name.empty())
         m_path_name.clear();
      m_path_name.assign(file.length(), L' ');
      copy(file.begin(), file.end(), m_path_name.begin());
#ifdef _WIN32
   wchar_t slash = L'\\';
   std::replace(m_path_name.begin(), m_path_name.end(), L'/', L'\\');
#else
   wchar_t slash = L'/';
#endif
      m_data_name = m_path_name.substr(m_path_name.find_last_of(slash)+1);
	  m_dir_name = m_path_name.substr(0, m_path_name.find_last_of(slash)+1);
       
       if (m_dir_name.size() > 1)
       {
           size_t ext_pos = m_path_name.find_last_of(L".");
           wstring ext = m_path_name.substr(ext_pos+1);
           transform(ext.begin(), ext.end(), ext.begin(), towlower);
           if (ext == L"json" || ext == L"n5fs_ch") {
               m_data_name = m_dir_name.substr(m_dir_name.substr(0, m_dir_name.size() - 1).find_last_of(slash)+1);
           }
           else if (ext == L"n5")
               m_dir_name = m_path_name + slash;
       }
   }
   m_id_string = m_path_name;
}

//Use Before Preprocess()
void BRKXMLReader::SetFile(wstring &file)
{
   m_path_name = file;
#ifdef _WIN32
   wchar_t slash = L'\\';
   std::replace(m_path_name.begin(), m_path_name.end(), L'/', L'\\');
#else
   wchar_t slash = L'/';
#endif
   m_data_name = m_path_name.substr(m_path_name.find_last_of(slash)+1);
   m_dir_name = m_path_name.substr(0, m_path_name.find_last_of(slash)+1);
    
    if (m_dir_name.size() > 1)
    {
        size_t ext_pos = m_path_name.find_last_of(L".");
        wstring ext = m_path_name.substr(ext_pos+1);
        transform(ext.begin(), ext.end(), ext.begin(), towlower);
        if (ext == L"json" || ext == L"n5fs_ch") {
            m_data_name = m_dir_name.substr(m_dir_name.substr(0, m_dir_name.size() - 1).find_last_of(slash)+1);
        }
        else if (ext == L"n5")
            m_dir_name = m_path_name + slash;
    }

   m_id_string = m_path_name;
}

//Use Before Preprocess()
void BRKXMLReader::SetDir(string &dir)
{
	if (!dir.empty())
	{
		if (!m_dir_name.empty())
			m_dir_name.clear();
		m_dir_name.assign(dir.length(), L' ');
		copy(dir.begin(), dir.end(), m_dir_name.begin());
		size_t pos = m_dir_name.find(L"://");
		if (pos != wstring::npos)
			m_isURL = true;

#ifdef _WIN32
		if (!m_isURL)
		{
			wchar_t slash = L'\\';
			std::replace(m_dir_name.begin(), m_dir_name.end(), L'/', L'\\');
		}
#endif
	}
}

//Use Before Preprocess()
void BRKXMLReader::SetDir(wstring &dir)
{
	if (!dir.empty())
	{
		m_dir_name = dir;
		size_t pos = m_dir_name.find(L"://");
		if (pos != wstring::npos)
			m_isURL = true;

#ifdef _WIN32
		if (!m_isURL)
		{
			wchar_t slash = L'\\';
			std::replace(m_dir_name.begin(), m_dir_name.end(), L'/', L'\\');
		}
#endif
	}
}


void BRKXMLReader::Preprocess()
{
	Clear();
	m_slice_num = 0;
	m_chan_num = 0;
	m_max_value = 0.0;

#ifdef _WIN32
	wchar_t slash = L'\\';
#else
	wchar_t slash = L'/';
#endif
	//separate path and name
	size_t pos = m_path_name.find_last_of(slash);
	wstring path = m_path_name.substr(0, pos+1);
	wstring name = m_path_name.substr(pos+1);
    
    size_t ext_pos = m_path_name.find_last_of(L".");
    wstring ext = m_path_name.substr(ext_pos+1);
    transform(ext.begin(), ext.end(), ext.begin(), towlower);
    
    if (ext == L"n5" || ext == L"json" || ext == L"n5fs_ch") {
        loadFSN5();
    }
    else if (ext == L"vvd") {
        if (m_doc.LoadFile(ws2s(m_path_name).c_str()) != 0){
            return;
        }
		
        tinyxml2::XMLElement *root = m_doc.RootElement();
        if (!root || strcmp(root->Name(), "BRK"))
            return;
        m_imageinfo = ReadImageInfo(root);

        if (root->Attribute("exMetadataPath"))
        {
            string str = root->Attribute("exMetadataPath");
            m_ex_metadata_path = s2ws(str);
        }
        if (root->Attribute("exMetadataURL"))
        {
            string str = root->Attribute("exMetadataURL");
            m_ex_metadata_url = s2ws(str);
        }

        ReadPyramid(root, m_pyramid);
	
        m_time_num = m_imageinfo.nFrame;
        m_chan_num = m_imageinfo.nChannel;
        m_copy_lv = m_imageinfo.copyableLv;
        m_mask_lv = m_imageinfo.maskLv;

        m_cur_time = 0;

        if(m_pyramid.empty()) return;

        m_xspc = m_pyramid[0].xspc;
        m_yspc = m_pyramid[0].yspc;
        m_zspc = m_pyramid[0].zspc;

        m_x_size = m_pyramid[0].imageW;
        m_y_size = m_pyramid[0].imageH;
        m_slice_num = m_pyramid[0].imageD;

        m_file_type = m_pyramid[0].file_type;

        m_level_num = m_pyramid.size();
        m_cur_level = 0;

        wstring cur_dir_name = m_path_name.substr(0, m_path_name.find_last_of(slash)+1);
        loadMetadata(m_path_name);
        loadMetadata(cur_dir_name + L"_metadata.xml");

        if (!m_ex_metadata_path.empty())
        {
            bool is_rel = false;
    #ifdef _WIN32
            if (m_ex_metadata_path.length() > 2 && m_ex_metadata_path[1] != L':')
                is_rel = true;
    #else
            if (m_ex_metadata_path[0] != L'/')
                is_rel = true;
    #endif
            if (is_rel)
                loadMetadata(cur_dir_name + m_ex_metadata_path);
            else
                loadMetadata(m_ex_metadata_path);
        }
    }

	SetInfo();

	//OutputInfo();
}

BRKXMLReader::ImageInfo BRKXMLReader::ReadImageInfo(tinyxml2::XMLElement *infoNode)
{
	ImageInfo iinfo;
	int ival;
	
    string strValue;
	
	ival = STOI(infoNode->Attribute("nChannel"));
	iinfo.nChannel = ival;

    ival = STOI(infoNode->Attribute("nFrame"));
	iinfo.nFrame = ival;

    ival = STOI(infoNode->Attribute("nLevel"));
	iinfo.nLevel = ival;

	if (infoNode->Attribute("CopyableLv"))
	{
		ival = STOI(infoNode->Attribute("CopyableLv"));
		iinfo.copyableLv = ival;
	}
	else
		iinfo.copyableLv = -1;
    
    if (infoNode->Attribute("MaskLv"))
    {
        ival = STOI(infoNode->Attribute("MaskLv"));
        iinfo.maskLv = ival;
    }
    else
        iinfo.maskLv = -1;

	return iinfo;
}

void BRKXMLReader::ReadPyramid(tinyxml2::XMLElement *lvRootNode, vector<LevelInfo> &pylamid)
{
	int ival;
	int level;

	tinyxml2::XMLElement *child = lvRootNode->FirstChildElement();
	while (child)
	{
		if (child->Name())
		{
			if (strcmp(child->Name(), "Level") == 0)
			{
				level = STOI(child->Attribute("lv"));
				if (level >= 0)
				{
					if(level + 1 > pylamid.size()) pylamid.resize(level + 1);
					ReadLevel(child, pylamid[level]);
				}
			}
		}
		child = child->NextSiblingElement();
	}
}

void BRKXMLReader::ReadLevel(tinyxml2::XMLElement* lvNode, LevelInfo &lvinfo)
{
	
	string strValue;

	lvinfo.imageW = STOI(lvNode->Attribute("imageW"));

	lvinfo.imageH = STOI(lvNode->Attribute("imageH"));

	lvinfo.imageD = STOI(lvNode->Attribute("imageD"));

	lvinfo.xspc = STOD(lvNode->Attribute("xspc"));

	lvinfo.yspc = STOD(lvNode->Attribute("yspc"));

	lvinfo.zspc = STOD(lvNode->Attribute("zspc"));

	lvinfo.bit_depth = STOI(lvNode->Attribute("bitDepth"));

	if (lvNode->Attribute("FileType"))
	{
		strValue = lvNode->Attribute("FileType");
        std::transform(strValue.begin(), strValue.end(), strValue.begin(), ::toupper);
		if (strValue == "RAW") lvinfo.file_type = BRICK_FILE_TYPE_RAW;
		else if (strValue == "JPEG") lvinfo.file_type = BRICK_FILE_TYPE_JPEG;
		else if (strValue == "ZLIB") lvinfo.file_type = BRICK_FILE_TYPE_ZLIB;
		else if (strValue == "H265") lvinfo.file_type = BRICK_FILE_TYPE_H265;
        else if (strValue == "GZIP") lvinfo.file_type = BRICK_FILE_TYPE_N5GZIP;
	}
	else lvinfo.file_type = BRICK_FILE_TYPE_NONE;

	tinyxml2::XMLElement *child = lvNode->FirstChildElement();
	while (child)
	{
		if (child->Name())
		{
			if (strcmp(child->Name(), "Bricks") == 0){
				lvinfo.brick_baseW = STOI(child->Attribute("brick_baseW"));

				lvinfo.brick_baseH = STOI(child->Attribute("brick_baseH"));

				lvinfo.brick_baseD = STOI(child->Attribute("brick_baseD"));

				ReadPackedBricks(child, lvinfo.bricks);
			}
			if (strcmp(child->Name(), "Files") == 0)  ReadFilenames(child, lvinfo.filename);
		}
		child = child->NextSiblingElement();
	}
}

void BRKXMLReader::ReadPackedBricks(tinyxml2::XMLElement* packNode, vector<BrickInfo *> &brks)
{
	int id;
	
	tinyxml2::XMLElement *child = packNode->FirstChildElement();
	while (child)
	{
		if (child->Name())
		{
			if (strcmp(child->Name(), "Brick") == 0)
			{
				id = STOI(child->Attribute("id"));

				if(id + 1 > brks.size())
					brks.resize(id + 1, NULL);

				if (!brks[id]) brks[id] = new BrickInfo();
				ReadBrick(child, *brks[id]);
			}
		}
		child = child->NextSiblingElement();
	}
}

void BRKXMLReader::ReadBrick(tinyxml2::XMLElement* brickNode, BrickInfo &binfo)
{
	int ival;
	double dval;
	
	string strValue;
		
	binfo.id = STOI(brickNode->Attribute("id"));

	binfo.x_size = STOI(brickNode->Attribute("width"));

	binfo.y_size = STOI(brickNode->Attribute("height"));

	binfo.z_size = STOI(brickNode->Attribute("depth"));

	binfo.x_start = STOI(brickNode->Attribute("st_x"));

	binfo.y_start = STOI(brickNode->Attribute("st_y"));

	binfo.z_start = STOI(brickNode->Attribute("st_z"));

	binfo.offset = STOLL(brickNode->Attribute("offset"));

	binfo.fsize = STOLL(brickNode->Attribute("size"));

	tinyxml2::XMLElement *child = brickNode->FirstChildElement();
	while (child)
	{
		if (child->Name())
		{
			if (strcmp(child->Name(), "tbox") == 0)
			{
				Readbox(child, binfo.tx0, binfo.ty0, binfo.tz0, binfo.tx1, binfo.ty1, binfo.tz1);
			}
			else if (strcmp(child->Name(), "bbox") == 0)
			{
				Readbox(child, binfo.bx0, binfo.by0, binfo.bz0, binfo.bx1, binfo.by1, binfo.bz1);
			}
		}
		child = child->NextSiblingElement();
	}
}

void BRKXMLReader::Readbox(tinyxml2::XMLElement* boxNode, double &x0, double &y0, double &z0, double &x1, double &y1, double &z1)
{
	x0 = STOD(boxNode->Attribute("x0"));
	
	y0 = STOD(boxNode->Attribute("y0"));

	z0 = STOD(boxNode->Attribute("z0"));

	x1 = STOD(boxNode->Attribute("x1"));
    
    y1 = STOD(boxNode->Attribute("y1"));
    
    z1 = STOD(boxNode->Attribute("z1"));
}

void BRKXMLReader::ReadFilenames(tinyxml2::XMLElement* fileRootNode, vector<vector<vector<FLIVR::FileLocInfo *>>> &filename)
{
	string str;
	int frame, channel, id;
    
    wstring prev_fname;
    
#ifdef _WIN32
    wchar_t slash = L'\\';
#else
    wchar_t slash = L'/';
#endif
    wstring cur_dir = m_path_name.substr(0, m_path_name.find_last_of(slash)+1);
    

	tinyxml2::XMLElement *child = fileRootNode->FirstChildElement();
	while (child)
	{
		if (child->Name())
		{
			if (strcmp(child->Name(), "File") == 0)
			{
				frame = STOI(child->Attribute("frame"));

				channel = STOI(child->Attribute("channel"));

				id = STOI(child->Attribute("brickID"));

				if(frame + 1 > filename.size())
					filename.resize(frame + 1);
				if(channel + 1 > filename[frame].size())
					filename[frame].resize(channel + 1);
				if(id + 1 > filename[frame][channel].size())
					filename[frame][channel].resize(id + 1, NULL);

				if (!filename[frame][channel][id])
						filename[frame][channel][id] = new FLIVR::FileLocInfo();

				if (child->Attribute("filename")) //this option will be deprecated
					str = child->Attribute("filename");
				else if (child->Attribute("filepath")) //use this
					str = child->Attribute("filepath");
				else if (child->Attribute("url")) //this option will be deprecated
					str = child->Attribute("url");

				bool url = false;
				bool rel = false;
				auto pos_u = str.find("://");
				if (pos_u != string::npos)
					url = true;
				if (!url)
				{
#ifdef _WIN32
					if (str.length() >= 2 && str[1] != L':')
						rel = true;
#else
					if (m_ex_metadata_path[0] != L'/')
						rel = true;
#endif
				}

				if (url) //url
				{
					filename[frame][channel][id]->filename = s2ws(str);
					filename[frame][channel][id]->isurl = true;
				}
				else if (rel) //relative path
				{
					filename[frame][channel][id]->filename = m_dir_name + s2ws(str);
					filename[frame][channel][id]->isurl = m_isURL;
                    
                    if (!m_isURL && prev_fname != filename[frame][channel][id]->filename)
                    {
						size_t pos = filename[frame][channel][id]->filename.find_last_of(slash);
						wstring name = filename[frame][channel][id]->filename.substr(pos + 1);
                        wstring secpath = cur_dir + name;
                        if (!exists(filename[frame][channel][id]->filename) && exists(secpath))
                            filename[frame][channel][id]->filename = secpath;
                    }
					prev_fname = filename[frame][channel][id]->filename;
				}
				else //absolute path
				{
					filename[frame][channel][id]->filename = s2ws(str);
					filename[frame][channel][id]->isurl = false;
                    
                    if (prev_fname != filename[frame][channel][id]->filename)
                    {
						size_t pos = filename[frame][channel][id]->filename.find_last_of(slash);
						wstring name = filename[frame][channel][id]->filename.substr(pos + 1);
						wstring secpath = cur_dir + name;
						if (!exists(filename[frame][channel][id]->filename) && exists(secpath))
							filename[frame][channel][id]->filename = secpath;
                    }
					prev_fname = filename[frame][channel][id]->filename;
				}

				filename[frame][channel][id]->offset = 0;
				if (child->Attribute("offset"))
					filename[frame][channel][id]->offset = STOLL(child->Attribute("offset"));
				filename[frame][channel][id]->datasize = 0;
				if (child->Attribute("datasize"))
					filename[frame][channel][id]->datasize = STOLL(child->Attribute("datasize"));
				
				if (child->Attribute("filetype"))
				{
                    str = child->Attribute("filetype");
                    std::transform(str.begin(), str.end(), str.begin(), ::toupper);
					if (str == "RAW") filename[frame][channel][id]->type = BRICK_FILE_TYPE_RAW;
					else if (str == "JPEG") filename[frame][channel][id]->type = BRICK_FILE_TYPE_JPEG;
					else if (str == "ZLIB") filename[frame][channel][id]->type = BRICK_FILE_TYPE_ZLIB;
					else if (str == "H265") filename[frame][channel][id]->type = BRICK_FILE_TYPE_H265;
                    else if (str == "GZIP") filename[frame][channel][id]->type = BRICK_FILE_TYPE_N5GZIP;
				}
				else
				{
					filename[frame][channel][id]->type = BRICK_FILE_TYPE_RAW;
					wstring fname = filename[frame][channel][id]->filename;
					auto pos = fname.find_last_of(L".");
					if (pos != wstring::npos && pos < fname.length()-1)
					{
						wstring ext = fname.substr(pos+1);
						transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
						if (ext == L"jpg" || ext == L"jpeg")
							filename[frame][channel][id]->type = BRICK_FILE_TYPE_JPEG;
						else if (ext == L"zlib")
							filename[frame][channel][id]->type = BRICK_FILE_TYPE_ZLIB;
						else if (ext == L"mp4")
							filename[frame][channel][id]->type = BRICK_FILE_TYPE_H265;
					}
				}

				std::wstringstream wss;
				wss << filename[frame][channel][id]->filename << L" " << filename[frame][channel][id]->offset;
				filename[frame][channel][id]->id_string = wss.str();
			}
		}
		child = child->NextSiblingElement();
	}
}

bool BRKXMLReader::loadMetadata(const wstring &file)
{
	string str;
	double dval;

	if (m_md_doc.LoadFile(ws2s(file).c_str()) != 0){
		return false;
	}
		
	tinyxml2::XMLElement *root = m_md_doc.RootElement();
	if (!root) return false;

	tinyxml2::XMLElement *md_node = NULL;
	if (strcmp(root->Name(), "Metadata") == 0)
		md_node = root;
	else
	{
		tinyxml2::XMLElement *child = root->FirstChildElement();
		while (child)
		{
			if (child->Name() && strcmp(child->Name(), "Metadata") == 0)
			{
				md_node = child;
			}
			child = child->NextSiblingElement();
		}
	}

	if (!md_node) return false;

	if (md_node->Attribute("ID"))
	{
		str = md_node->Attribute("ID");
		m_metadata_id = s2ws(str);
	}

	tinyxml2::XMLElement *child = md_node->FirstChildElement();
	while (child)
	{
		if (child->Name())
		{
			if (strcmp(child->Name(), "Landmark") == 0 && child->Attribute("name"))
			{
				Landmark lm;

				str = child->Attribute("name");
				lm.name = s2ws(str);

				lm.x = STOD(child->Attribute("x"));
				lm.y = STOD(child->Attribute("y"));
				lm.z = STOD(child->Attribute("z"));

				lm.spcx = STOD(child->Attribute("spcx"));
				lm.spcy = STOD(child->Attribute("spcy"));
				lm.spcz = STOD(child->Attribute("spcz"));

				m_landmarks.push_back(lm);
			}
			if (strcmp(child->Name(), "ROI_Tree") == 0)
			{
				LoadROITree(child);
			}
		}
		child = child->NextSiblingElement();
	}

	return true;
}

void BRKXMLReader::LoadROITree(tinyxml2::XMLElement *lvNode)
{
	if (!lvNode || strcmp(lvNode->Name(), "ROI_Tree"))
		return;

	if (!m_roi_tree.empty()) m_roi_tree.clear();

	int gid = -2;
	LoadROITree_r(lvNode, m_roi_tree, wstring(L""), gid);
}

void BRKXMLReader::LoadROITree_r(tinyxml2::XMLElement *lvNode, wstring& tree, const wstring& parent, int& gid)
{
	tinyxml2::XMLElement *child = lvNode->FirstChildElement();
	while (child)
	{
		if (child->Name() && child->Attribute("name"))
		{
			try
			{
				wstring name = s2ws(child->Attribute("name"));
				int r=0, g=0, b=0;
				if (strcmp(child->Name(), "Group") == 0)
				{
					wstringstream wss;
					wss << (parent.empty() ? L"" : parent + L".") << gid;
					wstring c_path = wss.str();

					tree += c_path + L"\n";
					tree += s2ws(child->Attribute("name")) + L"\n";
						
					//id and color
					wstringstream wss2;
					wss2 << gid << L" " << r << L" " << g << L" " << b << L"\n";
					tree += wss2.str();
					
					LoadROITree_r(child, tree, c_path, --gid);
				}
				if (strcmp(child->Name(), "ROI") == 0 && child->Attribute("id"))
				{
					string strid = child->Attribute("id");
					int id = boost::lexical_cast<int>(strid);
					if (id >= 0 && id < PALETTE_SIZE && child->Attribute("r") && child->Attribute("g") && child->Attribute("b"))
					{
						wstring c_path = (parent.empty() ? L"" : parent + L".") + s2ws(strid);

						tree += c_path + L"\n";
						tree += s2ws(child->Attribute("name")) + L"\n";

						string strR = child->Attribute("r");
						string strG = child->Attribute("g");
						string strB = child->Attribute("b");
						r = boost::lexical_cast<int>(strR);
						g = boost::lexical_cast<int>(strG);
						b = boost::lexical_cast<int>(strB);

						//id and color
						wstringstream wss;
						wss << id << L" " << r << L" " << g << L" " << b << L"\n";
						tree += wss.str();
					}
				}
			}
			catch (boost::bad_lexical_cast e)
			{
				cerr << "BRKXMLReader::LoadROITree_r(XMLElement *lvNode, wstring& tree, const wstring& parent): bad_lexical_cast" << endl;
			}
		}
		child = child->NextSiblingElement();
	}
}

void BRKXMLReader::GetLandmark(int index, wstring &name, double &x, double &y, double &z, double &spcx, double &spcy, double &spcz)
{
	if (index < 0 || m_landmarks.size() <= index) return;

	name = m_landmarks[index].name;
	x = m_landmarks[index].x;
	y = m_landmarks[index].y;
	z = m_landmarks[index].z;
	spcx = m_landmarks[index].spcx;
	spcy = m_landmarks[index].spcy;
	spcz = m_landmarks[index].spcz;
}

void BRKXMLReader::SetSliceSeq(bool ss)
{
   //do nothing
}

bool BRKXMLReader::GetSliceSeq()
{
   return false;
}

void BRKXMLReader::SetTimeSeq(bool ts)
{
   //do nothing
}

bool BRKXMLReader::GetTimeSeq()
{
   return false;
}

void BRKXMLReader::SetTimeId(wstring &id)
{
   m_time_id = id;
}

wstring BRKXMLReader::GetTimeId()
{
   return m_time_id;
}

void BRKXMLReader::SetCurTime(int t)
{
	if (t < 0) m_cur_time = 0;
	else if (t >= m_imageinfo.nFrame) m_cur_time = m_imageinfo.nFrame - 1;
	else m_cur_time = t;
}

void BRKXMLReader::SetCurChan(int c)
{
	if (c < 0) m_cur_chan = 0;
	else if (c >= m_imageinfo.nChannel) m_cur_chan = m_imageinfo.nChannel - 1;
	else m_cur_chan = c;
}

void BRKXMLReader::SetLevel(int lv)
{
	if(m_pyramid.empty()) return;
	
	if(lv < 0 || lv > m_level_num-1) return;
	m_cur_level = lv;

	m_xspc = m_pyramid[lv].xspc;
	m_yspc = m_pyramid[lv].yspc;
	m_zspc = m_pyramid[lv].zspc;

	m_x_size = m_pyramid[lv].imageW;
	m_y_size = m_pyramid[lv].imageH;
	m_slice_num = m_pyramid[lv].imageD;

	m_file_type = m_pyramid[lv].file_type;
}

void BRKXMLReader::SetBatch(bool batch)
{
#ifdef _WIN32
   wchar_t slash = L'\\';
#else
   wchar_t slash = L'/';
#endif
   if (batch)
   {
      //read the directory info
      wstring search_path = m_path_name.substr(0, m_path_name.find_last_of(slash)) + slash;
      FIND_FILES(search_path,L".vvd",m_batch_list,m_cur_batch);
      m_batch = true;
   }
   else
      m_batch = false;
}

int BRKXMLReader::LoadBatch(int index)
{
   int result = -1;
   if (index>=0 && index<(int)m_batch_list.size())
   {
      m_path_name = m_batch_list[index];
      Preprocess();
      result = index;
      m_cur_batch = result;
   }
   else
      result = -1;

   return result;
}

int BRKXMLReader::LoadOffset(int offset)
{
   int result = m_cur_batch + offset;

   if (offset > 0)
   {
      if (result<(int)m_batch_list.size())
      {
         m_path_name = m_batch_list[result];
         Preprocess();
         m_cur_batch = result;
      }
      else if (m_cur_batch<(int)m_batch_list.size()-1)
      {
         result = (int)m_batch_list.size()-1;
         m_path_name = m_batch_list[result];
         Preprocess();
         m_cur_batch = result;
      }
      else
         result = -1;
   }
   else if (offset < 0)
   {
      if (result >= 0)
      {
         m_path_name = m_batch_list[result];
         Preprocess();
         m_cur_batch = result;
      }
      else if (m_cur_batch > 0)
      {
         result = 0;
         m_path_name = m_batch_list[result];
         Preprocess();
         m_cur_batch = result;
      }
      else
         result = -1;
   }
   else
      result = -1;

   return result;
}

double BRKXMLReader::GetExcitationWavelength(int chan)
{
   return 0.0;
}

//This function does not load image data into Nrrd.
Nrrd* BRKXMLReader::ConvertNrrd(int t, int c, bool get_max)
{
	Nrrd *data = 0;

	//if (m_max_value > 0.0)
	//	m_scalar_scale = 65535.0 / m_max_value;
	m_scalar_scale = 1.0;

	if (m_xspc>0.0 && m_xspc<100.0 &&
		m_yspc>0.0 && m_yspc<100.0)
	{
		m_valid_spc = true;
		if (m_zspc<=0.0 || m_zspc>100.0)
			m_zspc = max(m_xspc, m_yspc);
	}
	else
	{
		m_valid_spc = false;
		m_xspc = 1.0;
		m_yspc = 1.0;
		m_zspc = 1.0;
	}

	if (t>=0 && t<m_time_num &&
		c>=0 && c<m_chan_num &&
		m_slice_num>0 &&
		m_x_size>0 &&
		m_y_size>0)
	{
		char dummy = 0;
		data = nrrdNew();
		if(m_pyramid[m_cur_level].bit_depth == 8)nrrdWrap(data, &dummy, nrrdTypeUChar, 3, (size_t)m_x_size, (size_t)m_y_size, (size_t)m_slice_num);
		else if(m_pyramid[m_cur_level].bit_depth == 16)nrrdWrap(data, &dummy, nrrdTypeUShort, 3, (size_t)m_x_size, (size_t)m_y_size, (size_t)m_slice_num);
		else if(m_pyramid[m_cur_level].bit_depth == 32)nrrdWrap(data, &dummy, nrrdTypeFloat, 3, (size_t)m_x_size, (size_t)m_y_size, (size_t)m_slice_num);
		nrrdAxisInfoSet(data, nrrdAxisInfoSpacing, m_xspc, m_yspc, m_zspc);
		nrrdAxisInfoSet(data, nrrdAxisInfoMax, m_xspc*m_x_size, m_yspc*m_y_size, m_zspc*m_slice_num);
		nrrdAxisInfoSet(data, nrrdAxisInfoMin, 0.0, 0.0, 0.0);
		nrrdAxisInfoSet(data, nrrdAxisInfoSize, (size_t)m_x_size, (size_t)m_y_size, (size_t)m_slice_num);
		data->data = NULL;//dangerous//
		
		m_cur_chan = c;
		m_cur_time = t;
	}

	if(get_max)
	{
		if(m_pyramid[m_cur_level].bit_depth == 8) m_max_value = 255.0;
		else if(m_pyramid[m_cur_level].bit_depth == 16) m_max_value = 65535.0;
		else if(m_pyramid[m_cur_level].bit_depth == 32) m_max_value = 1.0;
	}

	return data;
}

wstring BRKXMLReader::GetCurName(int t, int c)
{
   return wstring(L"");
}

FLIVR::FileLocInfo* BRKXMLReader::GetBrickFilePath(int fr, int ch, int id, int lv)
{
	int level = lv;
	int frame = fr;
	int channel = ch;
	int brickID = id;
	
	if(lv < 0 || lv >= m_level_num) level = m_cur_level;
	if(fr < 0 || fr >= m_time_num)  frame = m_cur_time;
	if(ch < 0 || ch >= m_chan_num)	channel = m_cur_chan;
	if(id < 0 || id >= m_pyramid[level].bricks.size()) brickID = 0;
	
	return m_pyramid[level].filename[frame][channel][brickID];
}

wstring BRKXMLReader::GetBrickFileName(int fr, int ch, int id, int lv)
{
	int level = lv;
	int frame = fr;
	int channel = ch;
	int brickID = id;
	
	if(lv < 0 || lv >= m_level_num) level = m_cur_level;
	if(fr < 0 || fr >= m_time_num)  frame = m_cur_time;
	if(ch < 0 || ch >= m_chan_num)	channel = m_cur_chan;
	if(id < 0 || id >= m_pyramid[level].bricks.size()) brickID = 0;

	#ifdef _WIN32
	wchar_t slash = L'\\';
#else
	wchar_t slash = L'/';
#endif
	if(m_isURL) slash = L'/';
	//separate path and name
	size_t pos = m_pyramid[level].filename[frame][channel][brickID]->filename.find_last_of(slash);
	wstring name = m_pyramid[level].filename[frame][channel][brickID]->filename.substr(pos+1);
	
	return name;
}

int BRKXMLReader::GetFileType(int lv)
{
	if(lv < 0 || lv > m_level_num-1) return m_file_type;

	return m_pyramid[lv].file_type;
}

void BRKXMLReader::OutputInfo()
{
	std::ofstream ofs;
	ofs.open("PyramidInfo.txt");

	ofs << "nChannel: " << m_imageinfo.nChannel << "\n";
	ofs << "nFrame: " << m_imageinfo.nFrame << "\n";
	ofs << "nLevel: " << m_imageinfo.nLevel << "\n\n";

	for(int i = 0; i < m_pyramid.size(); i++){
		ofs << "<Level: " << i << ">\n";
		ofs << "\timageW: " << m_pyramid[i].imageW << "\n";
		ofs << "\timageH: " << m_pyramid[i].imageH << "\n";
		ofs << "\timageD: " << m_pyramid[i].imageD << "\n";
		ofs << "\txspc: " << m_pyramid[i].xspc << "\n";
		ofs << "\tyspc: " << m_pyramid[i].yspc << "\n";
		ofs << "\tzspc: " << m_pyramid[i].zspc << "\n";
		ofs << "\tbrick_baseW: " << m_pyramid[i].brick_baseW << "\n";
		ofs << "\tbrick_baseH: " << m_pyramid[i].brick_baseH << "\n";
		ofs << "\tbrick_baseD: " << m_pyramid[i].brick_baseD << "\n";
		ofs << "\tbit_depth: " << m_pyramid[i].bit_depth << "\n";
		ofs << "\tfile_type: " << m_pyramid[i].file_type << "\n\n";
		for(int j = 0; j < m_pyramid[i].bricks.size(); j++){
			ofs << "\tBrick: " << " id = " <<  m_pyramid[i].bricks[j]->id
				<< " w = " << m_pyramid[i].bricks[j]->x_size
				<< " h = " << m_pyramid[i].bricks[j]->y_size
				<< " d = " << m_pyramid[i].bricks[j]->z_size
				<< " st_x = " << m_pyramid[i].bricks[j]->x_start
				<< " st_y = " << m_pyramid[i].bricks[j]->y_start
				<< " st_z = " << m_pyramid[i].bricks[j]->z_start
				<< " offset = " << m_pyramid[i].bricks[j]->offset
				<< " fsize = " << m_pyramid[i].bricks[j]->fsize << "\n";

			ofs << "\t\ttbox: "
				<< " x0 = " << m_pyramid[i].bricks[j]->tx0
				<< " y0 = " << m_pyramid[i].bricks[j]->ty0
				<< " z0 = " << m_pyramid[i].bricks[j]->tz0
				<< " x1 = " << m_pyramid[i].bricks[j]->tx1
				<< " y1 = " << m_pyramid[i].bricks[j]->ty1
				<< " z1 = " << m_pyramid[i].bricks[j]->tz1 << "\n";
			ofs << "\t\tbbox: "
				<< " x0 = " << m_pyramid[i].bricks[j]->bx0
				<< " y0 = " << m_pyramid[i].bricks[j]->by0
				<< " z0 = " << m_pyramid[i].bricks[j]->bz0
				<< " x1 = " << m_pyramid[i].bricks[j]->bx1
				<< " y1 = " << m_pyramid[i].bricks[j]->by1
				<< " z1 = " << m_pyramid[i].bricks[j]->bz1 << "\n";
		}
		ofs << "\n";
		for(int j = 0; j < m_pyramid[i].filename.size(); j++){
			for(int k = 0; k < m_pyramid[i].filename[j].size(); k++){
				for(int n = 0; n < m_pyramid[i].filename[j][k].size(); n++)
					ofs << "\t<Frame = " << j << " Channel = " << k << " ID = " << n << " Filepath = " << ws2s(m_pyramid[i].filename[j][k][n]->filename) << ">\n";
			}
		}
		ofs << "\n";
	}

	ofs << "Landmarks\n";
	for(int i = 0; i < m_landmarks.size(); i++){
		ofs << "\tName: " << ws2s(m_landmarks[i].name);
		ofs << " X: " << m_landmarks[i].x;
		ofs << " Y: " << m_landmarks[i].y;
		ofs << " Z: " << m_landmarks[i].z;
		ofs << " SpcX: " << m_landmarks[i].spcx;
		ofs << " SpcY: " << m_landmarks[i].spcy;
		ofs << " SpcZ: " << m_landmarks[i].spcz << "\n";
	}

	ofs.close();
}

void BRKXMLReader::build_bricks(vector<FLIVR::TextureBrick*> &tbrks, int lv)
{
	int lev;

	if(lv < 0 || lv > m_level_num-1) lev = m_cur_level;
	else lev = lv;

	// Initial brick size
	int bsize[3];

	bsize[0] = m_pyramid[lev].brick_baseW;
	bsize[1] = m_pyramid[lev].brick_baseH;
	bsize[2] = m_pyramid[lev].brick_baseD;

	bool force_pow2 = false;
	int max_texture_size = 65535;
	
	int numb[1];
	if (m_pyramid[lev].bit_depth == 8 || m_pyramid[lev].bit_depth == 16 || m_pyramid[lev].bit_depth == 32)
		numb[0] = m_pyramid[lev].bit_depth / 8;
	else
		numb[0] = 0;
	
	//further determine the max texture size
//	if (FLIVR::TextureRenderer::get_mem_swap())
//	{
//		double data_size = double(m_pyramid[lev].imageW)*double(m_pyramid[lev].imageH)*double(m_pyramid[lev].imageD)*double(numb[0])/1.04e6;
//		if (data_size > FLIVR::TextureRenderer::get_mem_limit() ||
//			data_size > FLIVR::TextureRenderer::get_large_data_size())
//			max_texture_size = FLIVR::TextureRenderer::get_force_brick_size();
//	}
	
	if(bsize[0] > max_texture_size || bsize[1] > max_texture_size || bsize[2] > max_texture_size) return;
	if(force_pow2 && (FLIVR::Pow2(bsize[0]) > bsize[0] || FLIVR::Pow2(bsize[1]) > bsize[1] || FLIVR::Pow2(bsize[2]) > bsize[2])) return;
	
	if(!tbrks.empty())
	{
		for(int i = 0; i < tbrks.size(); i++)
		{
			tbrks[i]->freeBrkData();
			delete tbrks[i];
		}
		tbrks.clear();
	}
	vector<BrickInfo *>::iterator bite = m_pyramid[lev].bricks.begin();
	while (bite != m_pyramid[lev].bricks.end())
	{
		FLIVR::BBox tbox(FLIVR::Point((*bite)->tx0, (*bite)->ty0, (*bite)->tz0), FLIVR::Point((*bite)->tx1, (*bite)->ty1, (*bite)->tz1));
		FLIVR::BBox bbox(FLIVR::Point((*bite)->bx0, (*bite)->by0, (*bite)->bz0), FLIVR::Point((*bite)->bx1, (*bite)->by1, (*bite)->bz1));

		double dx0, dy0, dz0, dx1, dy1, dz1;
		dx0 = (double)((*bite)->x_start) / m_pyramid[lev].imageW;
		dy0 = (double)((*bite)->y_start) / m_pyramid[lev].imageH;
		dz0 = (double)((*bite)->z_start) / m_pyramid[lev].imageD;
		dx1 = (double)((*bite)->x_start + (*bite)->x_size) / m_pyramid[lev].imageW;
		dy1 = (double)((*bite)->y_start + (*bite)->y_size) / m_pyramid[lev].imageH;
		dz1 = (double)((*bite)->z_start + (*bite)->z_size) / m_pyramid[lev].imageD;

		FLIVR::BBox dbox = FLIVR::BBox(FLIVR::Point(dx0, dy0, dz0), FLIVR::Point(dx1, dy1, dz1));

		//numc? gm_nrrd?
		FLIVR::TextureBrick *b = new FLIVR::TextureBrick(0, 0, (*bite)->x_size, (*bite)->y_size, (*bite)->z_size, 1, numb, 
														 (*bite)->x_start, (*bite)->y_start, (*bite)->z_start,
														 (*bite)->x_size, (*bite)->y_size, (*bite)->z_size, bbox, tbox, dbox, (*bite)->id, (*bite)->offset, (*bite)->fsize);
		tbrks.push_back(b);
		
		bite++;
	}

	return;
}

void BRKXMLReader::build_pyramid(vector<FLIVR::Pyramid_Level> &pyramid, vector<vector<vector<vector<FLIVR::FileLocInfo *>>>> &filenames, int t, int c)
{
	if (!pyramid.empty())
	{
		for (int i = 0; i < pyramid.size(); i++)
		{
			if (pyramid[i].data) pyramid[i].data.reset();
			for (int j = 0; j < pyramid[i].bricks.size(); j++)
				if (pyramid[i].bricks[j]) delete pyramid[i].bricks[j];
		}
		vector<FLIVR::Pyramid_Level>().swap(pyramid);
	}

	if(!filenames.empty())
	{
		for (int i = 0; i < filenames.size(); i++)
			for (int j = 0; j < filenames[i].size(); j++)
				for (int k = 0; k < filenames[i][j].size(); k++)
					for (int n = 0; n < filenames[i][j][k].size(); n++)
					if (filenames[i][j][k][n]) delete filenames[i][j][k][n];
		vector<vector<vector<vector<FLIVR::FileLocInfo *>>>>().swap(filenames);
	}

	pyramid.resize(m_pyramid.size());

	for (int i = 0; i < m_pyramid.size(); i++)
	{
		SetLevel(i);
		pyramid[i].data = Convert(t, c, false);
		build_bricks(pyramid[i].bricks);
		pyramid[i].filenames = &m_pyramid[i].filename[t][c];
		pyramid[i].filetype = GetFileType();
	}

	filenames.resize(m_pyramid.size());
	for (int i = 0; i < filenames.size(); i++)
	{
		filenames[i].resize(m_pyramid[i].filename.size());
		for (int j = 0; j < filenames[i].size(); j++)
		{
			filenames[i][j].resize(m_pyramid[i].filename[j].size());
			for (int k = 0; k < filenames[i][j].size(); k++)
			{
				filenames[i][j][k].resize(m_pyramid[i].filename[j][k].size());
				for (int n = 0; n < filenames[i][j][k].size(); n++)
				{
                    if (m_pyramid[i].filename[j][k][n])
                        filenames[i][j][k][n] = new FLIVR::FileLocInfo(*m_pyramid[i].filename[j][k][n]);
                    else
                        filenames[i][j][k][n] = nullptr;
				}
			}
		}
	}

}

void BRKXMLReader::SetInfo()
{
	wstringstream wss;
	
	wss << L"------------------------\n";
	wss << m_path_name << '\n';
	wss << L"File type: VVD\n";
	wss << L"Width: " << m_x_size << L'\n';
	wss << L"Height: " << m_y_size << L'\n';
	wss << L"Depth: " << m_slice_num << L'\n';
	wss << L"Channels: " << m_chan_num << L'\n';
	wss << L"Frames: " << m_time_num << L'\n';

	m_info = wss.str();
}

void BRKXMLReader::loadFSN5()
{
    size_t ext_pos = m_path_name.find_last_of(L".");
    wstring ext = m_path_name.substr(ext_pos+1);
    transform(ext.begin(), ext.end(), ext.begin(), towlower);
    boost::filesystem::path file_path(m_path_name);
    
	boost::filesystem::path::imbue(std::locale(std::locale(), new std::codecvt_utf8_utf16<wchar_t>()));
	boost::filesystem::path root_path(m_dir_name);

#ifdef _WIN32
	wchar_t slash = L'\\';
#else
	wchar_t slash = L'/';
#endif

	vector<double> pix_res(3, 1.0);
	auto root_attrpath = root_path / "attributes.json";
	std::ifstream ifs(root_attrpath.string());
	if (ifs.is_open()) {
		auto jf = json::parse(ifs);
		if (!jf.is_null() && jf.contains(PixelResolutionKey) && jf[PixelResolutionKey].contains(DimensionsKey))
			pix_res = jf[PixelResolutionKey][DimensionsKey].get<vector<double>>();
	}
    
    directory_iterator end_itr; // default construction yields past-the-end
    vector<wstring> ch_dirs;
    if (file_path.extension() == ".n5fs_ch") {
        ch_dirs.push_back((file_path.stem()).generic_wstring());
    }
    else
    {
        std::regex chdir_pattern("^c\\d+$");
        for (directory_iterator itr(root_path); itr != end_itr; ++itr)
        {
            if (is_directory(itr->status()))
            {
                if (regex_match(itr->path().filename().string(), chdir_pattern))
                    ch_dirs.push_back(itr->path().filename().wstring());
            }
        }
    }

	if (ch_dirs.empty())
		return;

	sort(ch_dirs.begin(), ch_dirs.end(),
		[](const wstring& x, const wstring& y) { return WSTOI(x.substr(1)) < WSTOI(y.substr(1)); });


	vector<vector<wstring>> relpaths;
	for (int i = 0; i < ch_dirs.size(); i++) {
		vector<wstring> scale_dirs;
		std::regex scdir_pattern("^s\\d+$");
		for (directory_iterator itr(root_path / ch_dirs[i]); itr != end_itr; ++itr)
		{
			if (is_directory(itr->status()))
			{
				if (regex_match(itr->path().filename().string(), scdir_pattern))
					scale_dirs.push_back(itr->path().filename().wstring());
			}
		}

		if (scale_dirs.empty())
			continue;

		sort(scale_dirs.begin(), scale_dirs.end(),
			[](const wstring& x, const wstring& y) { return WSTOI(x.substr(1)) < WSTOI(y.substr(1)); });

		int orgw = 0;
		int orgh = 0;
		int orgd = 0;
		if (m_pyramid.size() < scale_dirs.size())
			m_pyramid.resize(scale_dirs.size());
		for (int j = 0; j < scale_dirs.size(); j++) {
			auto attrpath = root_path / ch_dirs[i] / scale_dirs[j] / "attributes.json";
			DatasetAttributes* attr = parseDatasetMetadata(attrpath.wstring());

			LevelInfo& lvinfo = m_pyramid[j];
			if (i == 0) {
				lvinfo.imageW = attr->m_dimensions[0];

				lvinfo.imageH = attr->m_dimensions[1];

				lvinfo.imageD = attr->m_dimensions[2];

				if (i == 0 && j == 0) {
					orgw = lvinfo.imageW;
					orgh = lvinfo.imageH;
					orgd = lvinfo.imageD;
				}

				lvinfo.file_type = attr->m_compression;
                lvinfo.blosc_ctype = attr->m_blosc_param.ctype;
                lvinfo.blosc_clevel = attr->m_blosc_param.clevel;
                lvinfo.blosc_suffle = attr->m_blosc_param.suffle;
                lvinfo.blosc_blocksize = attr->m_blosc_param.blocksize;

				if (attr->m_pix_res[0] > 0.0)
					lvinfo.xspc = attr->m_pix_res[0];
				else
					lvinfo.xspc = pix_res[0] / ((double)lvinfo.imageW / orgw);

				if (attr->m_pix_res[1] > 0.0)
					lvinfo.yspc = attr->m_pix_res[1];
				else
					lvinfo.yspc = pix_res[1] / ((double)lvinfo.imageH / orgh);

				if (attr->m_pix_res[2] > 0.0)
					lvinfo.zspc = attr->m_pix_res[2];
				else
					lvinfo.zspc = pix_res[2] / ((double)lvinfo.imageD / orgd);

				lvinfo.bit_depth = attr->m_dataType;

				lvinfo.brick_baseW = attr->m_blockSize[0];

				lvinfo.brick_baseH = attr->m_blockSize[1];

				lvinfo.brick_baseD = attr->m_blockSize[2];

				vector<wstring> lvpaths;
				int ii, jj, kk;
				int mx, my, mz, mx2, my2, mz2, ox, oy, oz;
				double tx0, ty0, tz0, tx1, ty1, tz1;
				double bx1, by1, bz1;
				double dx0, dy0, dz0, dx1, dy1, dz1;
				const int overlapx = 0;
				const int overlapy = 0;
				const int overlapz = 0;
				size_t count = 0;
				size_t zcount = 0;
				for (kk = 0; kk < lvinfo.imageD; kk += lvinfo.brick_baseD)
				{
					if (kk) kk -= overlapz;
					size_t ycount = 0;
					for (jj = 0; jj < lvinfo.imageH; jj += lvinfo.brick_baseH)
					{
						if (jj) jj -= overlapy;
						size_t xcount = 0;
						for (ii = 0; ii < lvinfo.imageW; ii += lvinfo.brick_baseW)
						{
							BrickInfo* binfo = new BrickInfo();

							if (ii) ii -= overlapx;
							mx = min(lvinfo.brick_baseW, lvinfo.imageW - ii);
							my = min(lvinfo.brick_baseH, lvinfo.imageH - jj);
							mz = min(lvinfo.brick_baseD, lvinfo.imageD - kk);

							mx2 = mx;
							my2 = my;
							mz2 = mz;

							// Compute Texture Box.
							tx0 = ii ? ((mx2 - mx + overlapx / 2.0) / mx2) : 0.0;
							ty0 = jj ? ((my2 - my + overlapy / 2.0) / my2) : 0.0;
							tz0 = kk ? ((mz2 - mz + overlapz / 2.0) / mz2) : 0.0;

							tx1 = 1.0 - overlapx / 2.0 / mx2;
							if (mx < lvinfo.brick_baseW) tx1 = 1.0;
							if (lvinfo.imageW - ii == lvinfo.brick_baseW) tx1 = 1.0;

							ty1 = 1.0 - overlapy / 2.0 / my2;
							if (my < lvinfo.brick_baseH) ty1 = 1.0;
							if (lvinfo.imageH - jj == lvinfo.brick_baseH) ty1 = 1.0;

							tz1 = 1.0 - overlapz / 2.0 / mz2;
							if (mz < lvinfo.brick_baseD) tz1 = 1.0;
							if (lvinfo.imageD - kk == lvinfo.brick_baseD) tz1 = 1.0;

							binfo->tx0 = tx0;
							binfo->ty0 = ty0;
							binfo->tz0 = tz0;
							binfo->tx1 = tx1;
							binfo->ty1 = ty1;
							binfo->tz1 = tz1;

							// Compute BBox.
							bx1 = min((ii + lvinfo.brick_baseW - overlapx / 2.0) / (double)lvinfo.imageW, 1.0);
							if (lvinfo.imageW - ii == lvinfo.brick_baseW) bx1 = 1.0;

							by1 = min((jj + lvinfo.brick_baseH - overlapy / 2.0) / (double)lvinfo.imageH, 1.0);
							if (lvinfo.imageH - jj == lvinfo.brick_baseH) by1 = 1.0;

							bz1 = min((kk + lvinfo.brick_baseD - overlapz / 2.0) / (double)lvinfo.imageD, 1.0);
							if (lvinfo.imageD - kk == lvinfo.brick_baseD) bz1 = 1.0;

							binfo->bx0 = ii == 0 ? 0 : (ii + overlapx / 2.0) / (double)lvinfo.imageW;
							binfo->by0 = jj == 0 ? 0 : (jj + overlapy / 2.0) / (double)lvinfo.imageH;
							binfo->bz0 = kk == 0 ? 0 : (kk + overlapz / 2.0) / (double)lvinfo.imageD;
							binfo->bx1 = bx1;
							binfo->by1 = by1;
							binfo->bz1 = bz1;

							ox = ii - (mx2 - mx);
							oy = jj - (my2 - my);
							oz = kk - (mz2 - mz);

							binfo->id = count++;
							binfo->x_start = ox;
							binfo->y_start = oy;
							binfo->z_start = oz;
							binfo->x_size = mx2;
							binfo->y_size = my2;
							binfo->z_size = mz2;

							binfo->fsize = 0;
							binfo->offset = 0;

							if (count > lvinfo.bricks.size())
								lvinfo.bricks.resize(count, NULL);

							lvinfo.bricks[binfo->id] = binfo;

							wstringstream wss;
							wss << xcount << slash << ycount << slash << zcount;
							lvpaths.push_back(wss.str());

							xcount++;
						}
						ycount++;
					}
					zcount++;
				}
				relpaths.push_back(lvpaths);
			}

			if (1 > lvinfo.filename.size())
				lvinfo.filename.resize(1);
			if (i + 1 > lvinfo.filename[0].size())
				lvinfo.filename[0].resize(i + 1);
			if (relpaths[j].size() > lvinfo.filename[0][i].size())
				lvinfo.filename[0][i].resize(relpaths[j].size(), NULL);

			for (int pid = 0; pid < relpaths[j].size(); pid++)
			{
				wstringstream wss2;
				wss2 << m_dir_name << ch_dirs[i] << slash << scale_dirs[j] << slash << relpaths[j][pid];
                boost::filesystem::path br_file_path(wss2.str());
                
                FLIVR::FileLocInfo* fi = nullptr;
                if (boost::filesystem::exists(br_file_path))
                    fi = new FLIVR::FileLocInfo(wss2.str(), 0, 0, lvinfo.file_type, false, true, lvinfo.blosc_blocksize, lvinfo.blosc_clevel, lvinfo.blosc_ctype, lvinfo.blosc_suffle);
				lvinfo.filename[0][i][pid] = fi;
			}

			delete attr;
		}
	}

	m_imageinfo.nFrame = 1;
	m_imageinfo.nChannel = ch_dirs.size();
	m_imageinfo.copyableLv = m_pyramid.size() - 1;

	m_time_num = m_imageinfo.nFrame;
	m_chan_num = m_imageinfo.nChannel;
	m_copy_lv = m_imageinfo.copyableLv;

	m_cur_time = 0;

	if (m_pyramid.empty()) return;

	m_xspc = m_pyramid[0].xspc;
	m_yspc = m_pyramid[0].yspc;
	m_zspc = m_pyramid[0].zspc;

	m_x_size = m_pyramid[0].imageW;
	m_y_size = m_pyramid[0].imageH;
	m_slice_num = m_pyramid[0].imageD;

	m_file_type = m_pyramid[0].file_type;

	m_level_num = m_pyramid.size();
	m_cur_level = 0;

}

DatasetAttributes* BRKXMLReader::parseDatasetMetadata(wstring jpath)
{
    boost::filesystem::path::imbue(std::locale( std::locale(), new std::codecvt_utf8_utf16<wchar_t>()));
    boost::filesystem::path path(jpath);
    std::ifstream ifs(path.string());

	if (!ifs.is_open())
		return nullptr;
    
    auto jf = json::parse(ifs);

	if (jf.is_null() ||
		!jf.contains(DimensionsKey) ||
		!jf.contains(DataTypeKey) ||
		!jf.contains(BlockSizeKey))
		return nullptr;

	DatasetAttributes* ret = new DatasetAttributes();
    
    ret->m_dimensions = jf[DimensionsKey].get<vector<long>>();
    
	string str = jf[DataTypeKey].get<string>();
	if (str == "uint8")
		ret->m_dataType = 8;
	else if (str == "uint16")
		ret->m_dataType = 16;
	else
		ret->m_dataType = 0;
    
    ret->m_blockSize = jf[BlockSizeKey].get<vector<int>>();
    
	if (jf.contains(CompressionKey) &&
		(jf[CompressionKey].type() == json::value_t::number_integer || jf[CompressionKey].type() == json::value_t::number_unsigned))
		ret->m_compression = jf[CompressionKey].get<int>();
	if (jf.contains(PixelResolutionKey) && jf[PixelResolutionKey].contains(DimensionsKey))
		ret->m_pix_res = jf[PixelResolutionKey][DimensionsKey].get<vector<double>>();
	else
		ret->m_pix_res = vector<double>(3, -1.0);
    
    /* version 0 */
    if (jf.contains(CompressionKey) && jf[CompressionKey].contains(CompressionTypeKey)) {
        auto cptype = jf[CompressionKey][CompressionTypeKey].get<string>();
        if (cptype == "raw")
			ret->m_compression = 0;
        else if (cptype == "gzip")
			ret->m_compression = 1;
        else if (cptype == "bzip2")
			ret->m_compression = 2;
        else if (cptype == "lz4")
			ret->m_compression = 3;
        else if (cptype == "xz")
			ret->m_compression = 4;
        else if (cptype == "blosc")
        {
            ret->m_compression = 5;
            if (jf[CompressionKey].contains(BloscLevelKey))
                ret->m_blosc_param.clevel = jf[CompressionKey][BloscLevelKey].get<int>();
            else ret->m_blosc_param.clevel = 0;
            
            if (jf[CompressionKey].contains(BloscBlockSizeKey))
                ret->m_blosc_param.blocksize = jf[CompressionKey][BloscBlockSizeKey].get<int>();
            else ret->m_blosc_param.blocksize = 0;
            
            if (jf[CompressionKey].contains(BloscCompressionKey))
            {
                auto bcptype = jf[CompressionKey][BloscCompressionKey].get<string>();
                
                if (bcptype == BLOSC_BLOSCLZ_COMPNAME)
                    ret->m_blosc_param.ctype = BLOSC_BLOSCLZ;
                else if (bcptype == BLOSC_LZ4_COMPNAME)
                    ret->m_blosc_param.ctype = BLOSC_LZ4;
                else if (bcptype == BLOSC_LZ4HC_COMPNAME)
                    ret->m_blosc_param.ctype = BLOSC_LZ4HC;
                else if (bcptype == BLOSC_SNAPPY_COMPNAME)
                    ret->m_blosc_param.ctype = BLOSC_SNAPPY;
                else if (bcptype == BLOSC_ZLIB_COMPNAME)
                    ret->m_blosc_param.ctype = BLOSC_ZLIB;
                else if (bcptype == BLOSC_ZSTD_COMPNAME)
                    ret->m_blosc_param.ctype = BLOSC_ZSTD;
            }
            else ret->m_blosc_param.ctype = BLOSC_BLOSCLZ;
            
            if (jf[CompressionKey].contains(BloscShuffleKey))
                ret->m_blosc_param.suffle = jf[CompressionKey][BloscShuffleKey].get<int>();
            else ret->m_blosc_param.suffle = 0;
        }
    }

	switch (ret->m_compression)
	{
	case 0:
		ret->m_compression = BRICK_FILE_TYPE_RAW;
		break;
	case 1:
		ret->m_compression = BRICK_FILE_TYPE_N5GZIP;
		break;
	case 3:
		ret->m_compression = BRICK_FILE_TYPE_LZ4;
		break;
    case 5:
        ret->m_compression = BRICK_FILE_TYPE_N5BLOSC;
        break;
	default:
		ret->m_compression = BRICK_FILE_TYPE_NONE;
	}

	return ret;
}

/*
 template <class T> T BRKXMLReader::parseAttribute(string key, T clazz, const json j)
 {
 if (attribute != null && j.contains(attribute))
 return j.at(attribute).get<T>();
 else
 return null;
 }
 
 HashMap<String, JsonElement> BRKXMLReader::readAttributes(final Reader reader, final Gson gson) throws IOException {
 
 final Type mapType = new TypeToken<HashMap<String, JsonElement>>() {}.getType();
 final HashMap<String, JsonElement> map = gson.fromJson(reader, mapType);
 return map == null ? new HashMap<>() : map;
 }
 */
/**
 * Return a reasonable class for a {@link JsonPrimitive}.  Possible return
 * types are
 * <ul>
 * <li>boolean</li>
 * <li>double</li>
 * <li>String</li>
 * <li>Object</li>
 * </ul>
 *
 * @param jsonPrimitive
 * @return
 */
/*
 public static Class< ? > BRKXMLReader::classForJsonPrimitive(final JsonPrimitive jsonPrimitive) {
 
 if (jsonPrimitive.isBoolean())
 return boolean.class;
 else if (jsonPrimitive.isNumber())
 return double.class;
 else if (jsonPrimitive.isString())
 return String.class;
 else return Object.class;
 }
 */

/**
 * Best effort implementation of {@link N5Reader#listAttributes(String)}
 * with limited type resolution.  Possible return types are
 * <ul>
 * <li>null</li>
 * <li>boolean</li>
 * <li>double</li>
 * <li>String</li>
 * <li>Object</li>
 * <li>boolean[]</li>
 * <li>double[]</li>
 * <li>String[]</li>
 * <li>Object[]</li>
 * </ul>
 */
/*
 @Override
 public default Map<String, Class< ? >> BRKXMLReader::listAttributes(final String pathName) throws IOException {
 
 final HashMap<String, JsonElement> jsonElementMap = getAttributes(pathName);
 final HashMap<String, Class< ? >> attributes = new HashMap<>();
 jsonElementMap.forEach(
 (key, jsonElement) -> {
 final Class< ? > clazz;
 if (jsonElement.isJsonNull())
 clazz = null;
 else if (jsonElement.isJsonPrimitive())
 clazz = classForJsonPrimitive((JsonPrimitive)jsonElement);
 else if (jsonElement.isJsonArray()) {
 final JsonArray jsonArray = (JsonArray)jsonElement;
 if (jsonArray.size() > 0) {
 final JsonElement firstElement = jsonArray.get(0);
 if (firstElement.isJsonPrimitive())
 clazz = Array.newInstance(classForJsonPrimitive((JsonPrimitive)firstElement), 0).getClass();
 else
 clazz = Object[].class;
 }
 else
 clazz = Object[].class;
 }
 else
 clazz = Object.class;
 attributes.put(key, clazz);
 });
 return attributes;
 }
 */

map<wstring, wstring> BRKXMLReader::getAttributes(wstring pathName)
{
    map<wstring, wstring> mmap;
    path ppath(m_path_name + GETSLASH() + getAttributesPath(pathName));
    if (exists(pathName) && !exists(ppath))
        return mmap;
    
    //mmap = jf.fromJson(reader, mapType);
    return mmap;
}


DataBlock BRKXMLReader::readBlock(wstring pathName, const DatasetAttributes &datasetAttributes, const vector<long> gridPosition)
{
    DataBlock ret;
    
    path ppath(m_path_name + GETSLASH() + getDataBlockPath(pathName, gridPosition));
    if (!exists(ppath))
        return ret;
    
    return readBlock(ppath.wstring(), datasetAttributes, gridPosition);
}

vector<wstring> BRKXMLReader::list(wstring pathName)
{
    vector<wstring> ret;
    path parent_path(m_path_name + GETSLASH() + pathName);
    
    if (!exists(parent_path))
        return ret;
    
    directory_iterator end_itr; // default construction yields past-the-end
    
    for (directory_iterator itr(parent_path); itr != end_itr; ++itr)
    {
        if (is_directory(itr->status()))
        {
            auto p = relative(itr->path(), parent_path);
            ret.push_back(p.wstring());
        }
    }
    
    return ret;
}

/**
 * Constructs the path for a data block in a dataset at a given grid position.
 *
 * The returned path is
 * <pre>
 * $datasetPathName/$gridPosition[0]/$gridPosition[1]/.../$gridPosition[n]
 * </pre>
 *
 * This is the file into which the data block will be stored.
 *
 * @param datasetPathName
 * @param gridPosition
 * @return
 */
wstring BRKXMLReader::getDataBlockPath(wstring datasetPathName, const vector<long> &gridPosition) {
    
    wstringstream wss;
    wss << removeLeadingSlash(datasetPathName) << GETSLASH();
    for (int i = 0; i < gridPosition.size(); ++i)
        wss << gridPosition[i];
    
    return wss.str();
}

/**
 * Constructs the path for the attributes file of a group or dataset.
 *
 * @param pathName
 * @return
 */
wstring BRKXMLReader::getAttributesPath(wstring pathName) {
    
    return removeLeadingSlash(pathName) + L"attributes.json";
}

/**
 * Removes the leading slash from a given path and returns the corrected path.
 * It ensures correctness on both Unix and Windows, otherwise {@code pathName} is treated
 * as UNC path on Windows, and {@code Paths.get(pathName, ...)} fails with {@code InvalidPathException}.
 */
wstring BRKXMLReader::removeLeadingSlash(const wstring pathName)
{
    return (pathName.rfind(L"/", 0) == 0) || (pathName.rfind(L"\\", 0) == 0) ? pathName.substr(1) : pathName;
}
