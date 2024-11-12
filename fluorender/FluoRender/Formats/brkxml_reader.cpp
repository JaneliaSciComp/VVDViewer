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
#include <filesystem>
#include <regex>
#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include "boost/filesystem.hpp"

#include <wx/url.h>
#include <wx/file.h>
#include <wx/stdpaths.h>

namespace fs = std::filesystem;

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

// Write callback function for handling data received from libcurl
size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    std::ofstream* outputFile = static_cast<std::ofstream*>(userp);
    size_t totalSize = size * nmemb;
    outputFile->write(static_cast<char*>(contents), totalSize);
    return totalSize;
}

bool DownloadFile(std::string& url)
{
    CURL* curl;
    CURLcode res;

    // Initialize a curl session
    curl = curl_easy_init();
    if (curl)
    {
        wxString pathname = url;
#ifdef _WIN32
        wxString tmpfname = wxStandardPaths::Get().GetTempDir() + "\\" + pathname.Mid(pathname.Find(wxT('/'), true)).Mid(1);
#else
        wxString tmpfname = wxStandardPaths::Get().GetTempDir() + "/" + pathname.Mid(pathname.Find(wxT('/'), true)).Mid(1);
#endif
        std::ofstream outputFile(tmpfname.ToStdString(), std::ios::binary);
        if (!outputFile.is_open())
        {
            std::cerr << "Could not open file to write: " << tmpfname.ToStdString() << std::endl;
            return false;
        }

        // Set URL for curl
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

        // Set write callback function
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);

        // Set user data for the callback function
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &outputFile);

        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);

        // Perform the file download
        res = curl_easy_perform(curl);

        // Cleanup curl session
        curl_easy_cleanup(curl);

        // Close the output file
        outputFile.close();

        // Check if download was successful
        if (res != CURLE_OK)
        {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
            return false;
        }

        std::cout << "Download succeeded: " << tmpfname.ToStdString() << std::endl;

        url = tmpfname.ToStdString();

        return true;
    }
    return false;
}

bool DownloadToCurrentDir(wxString& filename)
{
    wxString pathname = filename;
    wxURL url(pathname);
    //if (!url.IsOk())
    //    return false;
    url.GetProtocol().SetTimeout(10);
    wxString suffix = pathname.Mid(pathname.Find('.', true)).MakeLower();
    if (url.GetError() != wxURL_NOERR)
        return false;
    wxInputStream* in = url.GetInputStream();
    if (!in || !in->IsOk()) return false;
#define DOWNLOAD_BUFSIZE 8192
    unsigned char buffer[DOWNLOAD_BUFSIZE];
    size_t count = -1;
    wxMemoryBuffer mem_buf;
    while (!in->Eof() && count != 0)
    {
        in->Read(buffer, DOWNLOAD_BUFSIZE - 1);
        count = in->LastRead();
        if (count > 0) mem_buf.AppendData(buffer, count);
    }

#ifdef _WIN32
    wxString tmpfname = wxStandardPaths::Get().GetTempDir() + "\\" + pathname.Mid(pathname.Find(wxT('/'), true)).Mid(1);
#else
    wxString tmpfname = wxStandardPaths::Get().GetTempDir() + "/" + pathname.Mid(pathname.Find(wxT('/'), true)).Mid(1);
#endif

    wxFile of(tmpfname, wxFile::write);
    of.Write(mem_buf.GetData(), mem_buf.GetDataLen());
    of.Close();

    filename = tmpfname;

    return true;
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
    
    m_bdv_setup_id = -1;
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

      size_t ext_pos = m_path_name.find_last_of(L".");
      wstring ext = m_path_name.substr(ext_pos + 1);
      transform(ext.begin(), ext.end(), ext.begin(), towlower);

      if (ext == L"zarr")
          m_dir_name = m_path_name + slash;
      else if (ext == L"zfs_ch") {
          m_data_name = m_path_name.substr(0, ext_pos);
          m_data_name = m_data_name.substr(m_data_name.find_last_of(slash) + 1);
          m_dir_name = m_path_name.substr(0, ext_pos) + slash;
      }
       
       if (m_dir_name.size() > 1)
       {
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

   size_t ext_pos = m_path_name.find_last_of(L".");
   wstring ext = m_path_name.substr(ext_pos + 1);
   transform(ext.begin(), ext.end(), ext.begin(), towlower);

   if (ext == L"zarr")
       m_dir_name = m_path_name + slash;
   else if (ext == L"zfs_ch") {
       m_data_name = m_path_name.substr(0, ext_pos);
       m_data_name = m_data_name.substr(m_data_name.find_last_of(slash) + 1);
       m_dir_name = m_path_name.substr(0, ext_pos) + slash;
   }
    
    if (m_dir_name.size() > 1)
    {
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
    
    if (ext == L"n5" || ext == L"json" || ext == L"n5fs_ch" || ext == L"xml") {
        loadFSN5();
    }
    else if (ext == L"zarr" || ext == L"zgroup" || ext == L"zfs_ch") {
        loadFSZarr();
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

    if (lvinfo.bit_depth == 8)
        lvinfo.nrrd_type = nrrdTypeUChar;
    else if (lvinfo.bit_depth == 16)
        lvinfo.nrrd_type = nrrdTypeUShort;
    else if (lvinfo.bit_depth == 32)
        lvinfo.nrrd_type = nrrdTypeFloat;

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
                    std::replace( str.begin(), str.end(), '\\', '/');
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
	//m_scalar_scale = 1.0;

	if (m_xspc > 0.0 && m_yspc > 0.0)
	{
		m_valid_spc = true;
		if (m_zspc <= 0.0)
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
		nrrdWrap(data, &dummy, m_pyramid[m_cur_level].nrrd_type, 3, (size_t)m_x_size, (size_t)m_y_size, (size_t)m_slice_num);
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
        if (c >= 0 && c < m_chan_maxs.size() && m_chan_maxs[c] > 0.0) m_max_value = m_chan_maxs[c];
		else if(m_pyramid[m_cur_level].bit_depth == 8) m_max_value = 255.0;
		else if(m_pyramid[m_cur_level].bit_depth == 16) m_max_value = 65535.0;
        else if(m_pyramid[m_cur_level].nrrd_type == nrrdTypeInt || m_pyramid[m_cur_level].nrrd_type == nrrdTypeUInt) m_max_value = 100000.0;
        else if (m_pyramid[m_cur_level].nrrd_type == nrrdTypeFloat || m_pyramid[m_cur_level].nrrd_type == nrrdTypeDouble) {
            m_max_value = 4096.0;
            m_scalar_scale = 1.0 / m_max_value;
        }
		else m_max_value = 1.0;
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
	if (m_pyramid[lev].bit_depth == 8 || m_pyramid[lev].bit_depth == 16 || m_pyramid[lev].bit_depth == 32 || m_pyramid[lev].bit_depth == 64)
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
        if ((*bite))
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
        }
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

void BRKXMLReader::loadFSZarr()
{
#ifdef _WIN32
    wchar_t slash = L'\\';
#else
    wchar_t slash = L'/';
#endif

    size_t ext_pos = m_path_name.find_last_of(L".");
    wstring ext = m_path_name.substr(ext_pos + 1);
    transform(ext.begin(), ext.end(), ext.begin(), towlower);
    boost::filesystem::path file_path(m_path_name);

    //boost::filesystem::path::imbue(std::locale(std::locale(), new std::codecvt_utf8_utf16<wchar_t>()));
    boost::filesystem::path root_path(m_dir_name);

    vector<double> pix_res(3, 1.0);

    auto root_attrpath = root_path / ".zattrs";

    if (!exists(root_attrpath)) {
        string wxpath = root_attrpath.generic_string();
        if (DownloadFile(wxpath)) {
            m_isURL = true;
            root_attrpath = wxpath;
        }
    }

    vector<string> ch_colors;
    vector<double> ch_maxs;
    vector<double> ch_offsets;

    std::ifstream ifs(root_attrpath.string());
    if (ifs.is_open()) {
        auto jf = json::parse(ifs);
        if (!jf.is_null()) {
            if (jf.contains(MultiScalesKey) && jf[MultiScalesKey][0].contains(CoordinateTransformationsKey)) {
                for (auto& elem : jf[MultiScalesKey][0][CoordinateTransformationsKey]) {
                    if (elem.contains(ScaleKey)) {
                        pix_res = elem[ScaleKey].get<vector<double>>();
                        reverse(pix_res.begin(), pix_res.end());
                    }
                }
            }
            if (jf.contains(OmeroKey) && jf[OmeroKey].contains(ChannelsKey)) {
                for (auto& elem : jf[OmeroKey][ChannelsKey]) {
                    if (elem.contains(ColorKey)) {
                        string rgb_str = elem[ColorKey].get<string>();
                        ch_colors.push_back(rgb_str);
                    }
                    if (elem.contains(WindowKey) && elem[WindowKey].contains(MaxKey)) {
                        double max_val = elem[WindowKey][MaxKey].get<double>();
                        if (max_val > 0.0) {
                            ch_maxs.push_back(max_val);
                            if (elem.contains(WindowKey) && elem[WindowKey].contains(EndKey)) {
                                double end_val = elem[WindowKey][EndKey].get<double>();
                                if (end_val <= max_val)
                                    ch_offsets.push_back(end_val/max_val);
                            }
                        }
                    }
                }
            }
        }
    }

    ReadResolutionPyramidFromSingleZarrDataset(m_dir_name, pix_res);

    vector<wstring> ch_dirs;
    if (m_imageinfo.nChannel > 1) {
        for (int c = 0; c < m_imageinfo.nChannel; c++)
            ch_dirs.push_back(L"_Ch" + to_wstring(c));
    }
    else
        ch_dirs.push_back(L"");

    m_chan_names = ch_dirs;
    m_chan_cols = ch_colors;
    m_chan_maxs = ch_maxs;
    m_chan_offsets = ch_offsets;

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

void BRKXMLReader::loadFSN5()
{
    size_t ext_pos = m_path_name.find_last_of(L".");
    wstring ext = m_path_name.substr(ext_pos+1);
    transform(ext.begin(), ext.end(), ext.begin(), towlower);
    boost::filesystem::path file_path(m_path_name);
    
	//boost::filesystem::path::imbue(std::locale(std::locale(), new std::codecvt_utf8_utf16<wchar_t>()));
	boost::filesystem::path root_path(m_dir_name);

	vector<double> pix_res(3, 1.0);
    bool pix_res_found = false;
	auto root_attrpath = root_path / "attributes.json";
	std::ifstream ifs(root_attrpath.string());
	if (ifs.is_open()) {
		auto jf = json::parse(ifs);
        if (!jf.is_null() && jf.contains(PixelResolutionKey) && jf[PixelResolutionKey].contains(DimensionsKey)) {
            pix_res = jf[PixelResolutionKey][DimensionsKey].get<vector<double>>();
            pix_res_found = true;
        }
	}
    
    directory_iterator end_itr; // default construction yields past-the-end
    vector<wstring> ch_dirs;
    if (file_path.extension() == ".n5fs_ch") {
        ch_dirs.push_back(file_path.stem().wstring());
        if (!m_bdv_metadata_path.empty())
        {
            wstring ch_name = file_path.stem().wstring();
            std::regex chdir_pattern("^setup\\d+$");
            if (regex_match(ws2s(ch_name), chdir_pattern))
            {
                m_bdv_setup_id = WSTOI(ch_name.substr(5));
                tinyxml2::XMLDocument xmldoc;
                if (xmldoc.LoadFile(ws2s(m_bdv_metadata_path).c_str()) != 0){
                    return;
                }
                ReadBDVResolutions(xmldoc, m_bdv_resolutions);
                ReadBDVViewRegistrations(xmldoc, m_bdv_view_transforms);
                
                if (m_bdv_resolutions.size() > m_bdv_setup_id && m_bdv_resolutions[m_bdv_setup_id].size() >= 3)
                {
                    pix_res[0] = m_bdv_resolutions[m_bdv_setup_id][0];
                    pix_res[1] = m_bdv_resolutions[m_bdv_setup_id][1];
                    pix_res[2] = m_bdv_resolutions[m_bdv_setup_id][2];
                }
            }
        }
    }
    else if (file_path.extension() == ".xml") {
        m_bdv_metadata_path = file_path.wstring();
        tinyxml2::XMLDocument xmldoc;
        if (xmldoc.LoadFile(file_path.string().c_str()) != 0){
            return;
        }
        string r_n5path = ReadBDVFilePath(xmldoc);
        ReadBDVResolutions(xmldoc, m_bdv_resolutions);
        ReadBDVViewRegistrations(xmldoc, m_bdv_view_transforms);
        root_path = root_path / r_n5path;
        std::regex chdir_pattern("^setup\\d+$");
        for (directory_iterator itr(root_path); itr != end_itr; ++itr)
        {
            if (is_directory(itr->status()))
            {
                if (regex_match(itr->path().filename().string(), chdir_pattern))
                    ch_dirs.push_back(itr->path().filename().wstring());
            }
        }
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
        ch_dirs.push_back(L"");
    
    if (ch_dirs.empty())
    {
        m_error_msg = L"Error (N5 Reader): No channel exists.";
		return;
    }

	sort(ch_dirs.begin(), ch_dirs.end(),
		[](const wstring& x, const wstring& y) { return WSTOI(x.substr(1)) < WSTOI(y.substr(1)); });

    m_chan_names = ch_dirs;

	vector<vector<wstring>> relpaths;
	for (int i = 0; i < ch_dirs.size(); i++) {
        if (m_bdv_metadata_path.empty())
        {
            vector<double> ch_pix_res = pix_res;
            if (!pix_res_found) {
                auto attrpath = root_path / ch_dirs[i] / "attributes.json";
                std::ifstream ifs(attrpath.string());
                if (ifs.is_open()) {
                    auto jf = json::parse(ifs);
                    if (!jf.is_null() && jf.contains(PixelResolutionKey) && jf[PixelResolutionKey].contains(DimensionsKey)) {
                        ch_pix_res = jf[PixelResolutionKey][DimensionsKey].get<vector<double>>();
                    }
                }
            }
            boost::filesystem::path pyramid_abs_path = root_path / ch_dirs[i];
            ReadResolutionPyramidFromSingleN5Dataset(pyramid_abs_path.wstring(), 0, i, ch_pix_res);
        }
        else
        {
            vector<wstring> frame_dirs;
            std::regex fdir_pattern("^timepoint\\d+$");
            for (directory_iterator itr(root_path / ch_dirs[i]); itr != end_itr; ++itr)
            {
                if (is_directory(itr->status()))
                {
                    if (regex_match(itr->path().filename().string(), fdir_pattern))
                        frame_dirs.push_back(itr->path().filename().wstring());
                }
            }
            
            if (frame_dirs.empty())
                continue;
            
            sort(frame_dirs.begin(), frame_dirs.end(),
                 [](const wstring& x, const wstring& y) { return WSTOI(x.substr(5)) < WSTOI(y.substr(5)); });
            
            for (int f = 0; f < frame_dirs.size(); f++)
            {
                boost::filesystem::path pyramid_abs_path = root_path / ch_dirs[i] / frame_dirs[f];
                ReadResolutionPyramidFromSingleN5Dataset(pyramid_abs_path.wstring(), f, i, pix_res);
            }
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

void BRKXMLReader::ReadResolutionPyramidFromSingleZarrDataset(wstring root_dir, vector<double> pix_res)
{
#ifdef _WIN32
    wchar_t slash = L'\\';
#else
    wchar_t slash = L'/';
#endif

    boost::filesystem::path root_path(root_dir);
    directory_iterator end_itr;

    vector<vector<wstring>> relpaths;

    vector<wstring> scale_dirs;

    vector<vector<double>> pix_res_array;

    auto root_attrpath = root_path / ".zattrs";

    bool is_url = false;

    if (!exists(root_attrpath)) {
        string wxpath = root_attrpath.generic_string();
        if (DownloadFile(wxpath)) {
            is_url = true;
        }
        root_attrpath = wxpath;
    }

    std::ifstream ifs(root_attrpath.string());
    if (ifs.is_open()) {
        auto jf = json::parse(ifs);
        if (!jf.is_null() && jf.contains(MultiScalesKey) && jf[MultiScalesKey][0].contains(DatasetsKey)) {
            for (auto& elem : jf[MultiScalesKey][0][DatasetsKey]) {
                if (elem.contains(PathKey)) {
                    string path_val = elem[PathKey].get<string>();
                    scale_dirs.push_back(s2ws(path_val));
                    vector<double> p_res(3, -1.0);
                    if (elem.contains(CoordinateTransformationsKey)) {
                        for (auto& tr_elem : elem[CoordinateTransformationsKey]) {
                            if (tr_elem.contains(ScaleKey)) {
                                p_res = tr_elem[ScaleKey].get<vector<double>>();
                                reverse(p_res.begin(), p_res.end());
                            }
                        }
                    }
                    pix_res_array.push_back(p_res);
                }
            }
        }
    }
    
    if (scale_dirs.empty()) {
        auto metadata_path = root_path / ".zarray";
        if (is_url) {
            string wxpath = metadata_path.generic_string();
            if (DownloadFile(wxpath)) {
                is_url = true;
            }
            metadata_path = wxpath;
        }
        if (boost::filesystem::exists(metadata_path)) {
            scale_dirs.push_back(L".");
            vector<double> p_res = vector<double>(3, -1.0);
            pix_res_array.push_back(p_res);
        }
        else
            return;
    }

    sort(scale_dirs.begin(), scale_dirs.end(),
        [](const wstring& x, const wstring& y) { return WSTOI(x.substr(1)) < WSTOI(y.substr(1)); });

    int orgw = 0;
    int orgh = 0;
    int orgd = 0;
    if (m_pyramid.size() < scale_dirs.size())
        m_pyramid.resize(scale_dirs.size());

    for (int j = 0; j < scale_dirs.size(); j++) {
        auto attrpath = root_path / scale_dirs[j] / ".zarray";
        if (is_url) {
            if (scale_dirs[j] == L".")
                attrpath = root_path / ".zarray";
            string wxpath = attrpath.generic_string();
            if (DownloadFile(wxpath)) {
                is_url = true;
            }
            attrpath = wxpath;
        }
        DatasetAttributes* attr = parseDatasetMetadataZarr(attrpath.wstring());

        if (j == 0) {
            if (attr->m_dimensions.size() >= 4)
                m_imageinfo.nChannel = attr->m_dimensions[3];
            if (attr->m_dimensions.size() >= 5)
                m_imageinfo.nFrame = attr->m_dimensions[4];
            if (attr->m_dimensions.size() <= 3) {
                m_imageinfo.nFrame = 1;
                m_imageinfo.nChannel = 1;
            }
        }

        LevelInfo& lvinfo = m_pyramid[j];

		lvinfo.endianness = attr->m_is_big_endian ? BRICK_ENDIAN_BIG : BRICK_ENDIAN_LITTLE;
		lvinfo.is_row_major = attr->m_is_row_major;

		lvinfo.imageW = attr->m_dimensions[0];

		lvinfo.imageH = attr->m_dimensions[1];

		lvinfo.imageD = attr->m_dimensions[2];

		if (j == 0) {
			orgw = lvinfo.imageW;
			orgh = lvinfo.imageH;
			orgd = lvinfo.imageD;
		}

		lvinfo.file_type = attr->m_compression;
		lvinfo.blosc_ctype = attr->m_blosc_param.ctype;
		lvinfo.blosc_clevel = attr->m_blosc_param.clevel;
		lvinfo.blosc_suffle = attr->m_blosc_param.suffle;
		lvinfo.blosc_blocksize = attr->m_blosc_param.blocksize;

		if (pix_res_array[j][0] > 0.0)
			lvinfo.xspc = pix_res[0] * pix_res_array[j][0];
		else
			lvinfo.xspc = pix_res[0] / ((double)lvinfo.imageW / orgw);

		if (pix_res_array[j][1] > 0.0)
			lvinfo.yspc = pix_res[1] * pix_res_array[j][1];
		else
			lvinfo.yspc = pix_res[1] / ((double)lvinfo.imageH / orgh);

		if (pix_res_array[j][2] > 0.0)
			lvinfo.zspc = pix_res[2] * pix_res_array[j][2];
		else
			lvinfo.zspc = pix_res[2] / ((double)lvinfo.imageD / orgd);

		if (attr->m_dataType == nrrdTypeChar || attr->m_dataType == nrrdTypeUChar)
			lvinfo.bit_depth = 8;
		else if (attr->m_dataType == nrrdTypeShort || attr->m_dataType == nrrdTypeUShort)
			lvinfo.bit_depth = 16;
		else if (attr->m_dataType == nrrdTypeInt || attr->m_dataType == nrrdTypeUInt || attr->m_dataType == nrrdTypeFloat)
			lvinfo.bit_depth = 32;
		else if (attr->m_dataType == nrrdTypeDouble)
			lvinfo.bit_depth = 64;

		lvinfo.nrrd_type = attr->m_dataType;

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
					//wss << xcount << slash << ycount << slash << zcount;
					wss << zcount << slash << ycount << slash << xcount;
					lvpaths.push_back(wss.str());

					xcount++;
				}
				ycount++;
			}
			zcount++;
		}
		relpaths.push_back(lvpaths);

        if (m_imageinfo.nFrame > lvinfo.filename.size())
            lvinfo.filename.resize(m_imageinfo.nFrame);
        for (int f = 0; f < lvinfo.filename.size(); f++) 
        {
            if (m_imageinfo.nChannel > lvinfo.filename[f].size())
                lvinfo.filename[f].resize(m_imageinfo.nChannel);
            for (int c = 0; c < lvinfo.filename[f].size(); c++)
            {
                if (relpaths[j].size() > lvinfo.filename[f][c].size())
                    lvinfo.filename[f][c].resize(relpaths[j].size(), NULL);

                for (int pid = 0; pid < relpaths[j].size(); pid++)
                {
                    boost::filesystem::path br_file_path(root_path);
                    if (!is_url || scale_dirs[j] != L".")
                        br_file_path = br_file_path / scale_dirs[j];
                    if (attr->m_dimensions.size() >= 5)
                        br_file_path = br_file_path / to_string(f);
                    if (attr->m_dimensions.size() >= 4)
                        br_file_path = br_file_path / to_string(c);
                    br_file_path = br_file_path / relpaths[j][pid];
                    FLIVR::FileLocInfo* fi = nullptr;
                    fi = new FLIVR::FileLocInfo(is_url ? br_file_path.generic_wstring() : br_file_path.wstring(), 0, 0, lvinfo.file_type, is_url, false, lvinfo.brick_baseW, lvinfo.brick_baseH, lvinfo.brick_baseD, lvinfo.blosc_clevel, lvinfo.blosc_ctype, lvinfo.blosc_suffle, lvinfo.endianness, lvinfo.is_row_major);
                    lvinfo.filename[f][c][pid] = fi;
                }
            }
        }

        delete attr;
    }
}

void BRKXMLReader::ReadResolutionPyramidFromSingleN5Dataset(wstring root_dir, int f, int c, vector<double> pix_res)
{
#ifdef _WIN32
    wchar_t slash = L'\\';
#else
    wchar_t slash = L'/';
#endif
    
    boost::filesystem::path root_path(root_dir);
    directory_iterator end_itr;
    
    vector<vector<wstring>> relpaths;
    
    vector<wstring> scale_dirs;
    std::regex scdir_pattern("^s\\d+$");
    for (directory_iterator itr(root_path); itr != end_itr; ++itr)
    {
        if (is_directory(itr->status()))
        {
            if (regex_match(itr->path().filename().string(), scdir_pattern))
                scale_dirs.push_back(itr->path().filename().wstring());
        }
    }
    
    if (scale_dirs.empty()) {
        bool is_data = false;
        std::regex datadir_pattern("^\\d+$");
        for (directory_iterator itr(root_path); itr != end_itr; ++itr)
        {
            if (is_directory(itr->status()))
            {
                if (regex_match(itr->path().filename().string(), datadir_pattern))
                {
                    is_data = true;
                    break;
                }
            }
        }

        if (is_data)
            scale_dirs.push_back(L".");
        else
            return;
    }
    
    sort(scale_dirs.begin(), scale_dirs.end(),
         [](const wstring& x, const wstring& y) { return WSTOI(x.substr(1)) < WSTOI(y.substr(1)); });
    
    int orgw = 0;
    int orgh = 0;
    int orgd = 0;
    if (m_pyramid.size() < scale_dirs.size())
        m_pyramid.resize(scale_dirs.size());
    for (int j = 0; j < scale_dirs.size(); j++) {
        auto attrpath = root_path / scale_dirs[j] / "attributes.json";
        DatasetAttributes* attr = parseDatasetMetadataN5(attrpath.wstring());
        
        LevelInfo& lvinfo = m_pyramid[j];
        if (c == 0 && f == 0) {
            lvinfo.endianness = attr->m_is_big_endian ? BRICK_ENDIAN_BIG : BRICK_ENDIAN_LITTLE;
            lvinfo.is_row_major = attr->m_is_row_major;

            lvinfo.imageW = attr->m_dimensions[0];
            
            lvinfo.imageH = attr->m_dimensions[1];
            
            lvinfo.imageD = attr->m_dimensions[2];
            
            if (c == 0 && j == 0) {
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
            else if (attr->m_downsampling_factors[0] > 0.0)
                lvinfo.xspc = pix_res[0] * attr->m_downsampling_factors[0];
            else
                lvinfo.xspc = pix_res[0] / ((double)lvinfo.imageW / orgw);

            if (attr->m_pix_res[1] > 0.0)
                lvinfo.yspc = attr->m_pix_res[1];
            else if (attr->m_downsampling_factors[1] > 0.0)
                lvinfo.yspc = pix_res[1] * attr->m_downsampling_factors[1];
            else
                lvinfo.yspc = pix_res[1] / ((double)lvinfo.imageH / orgh);

            if (attr->m_pix_res[2] > 0.0)
                lvinfo.zspc = attr->m_pix_res[2];
            else if (attr->m_downsampling_factors[2] > 0.0)
                lvinfo.zspc = pix_res[2] * attr->m_downsampling_factors[2];
            else
                lvinfo.zspc = pix_res[2] / ((double)lvinfo.imageD / orgd);
            
            if (attr->m_dataType == nrrdTypeChar || attr->m_dataType == nrrdTypeUChar)
                lvinfo.bit_depth = 8;
            else if (attr->m_dataType == nrrdTypeShort || attr->m_dataType == nrrdTypeUShort)
                lvinfo.bit_depth = 16;
            else if (attr->m_dataType == nrrdTypeInt || attr->m_dataType == nrrdTypeUInt || attr->m_dataType == nrrdTypeFloat)
                lvinfo.bit_depth = 32;
            else if (attr->m_dataType == nrrdTypeDouble)
                lvinfo.bit_depth = 64;

            lvinfo.nrrd_type = attr->m_dataType;
            
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
        
        if (f + 1 > lvinfo.filename.size())
            lvinfo.filename.resize(f + 1);
        if (c + 1 > lvinfo.filename[f].size())
            lvinfo.filename[f].resize(c + 1);
        if (relpaths[j].size() > lvinfo.filename[f][c].size())
            lvinfo.filename[f][c].resize(relpaths[j].size(), NULL);
        
        for (int pid = 0; pid < relpaths[j].size(); pid++)
        {
            boost::filesystem::path br_file_path(root_path / scale_dirs[j] / relpaths[j][pid]);
            FLIVR::FileLocInfo* fi = nullptr;
            fi = new FLIVR::FileLocInfo(br_file_path.wstring(), 0, 0, lvinfo.file_type, false, true, lvinfo.blosc_blocksize, lvinfo.blosc_blocksize, lvinfo.blosc_blocksize, lvinfo.blosc_clevel, lvinfo.blosc_ctype, lvinfo.blosc_suffle, lvinfo.endianness, lvinfo.is_row_major);
            lvinfo.filename[f][c][pid] = fi;
        }
        
        delete attr;
    }
}

bool BRKXMLReader::GetZarrChannelPaths(wstring zarr_path, vector<wstring>& output)
{
#ifdef _WIN32
    wchar_t slash = L'\\';
#else
    wchar_t slash = L'/';
#endif

    size_t ext_pos = zarr_path.find_last_of(L".");
    wstring ext = zarr_path.substr(ext_pos + 1);
    transform(ext.begin(), ext.end(), ext.begin(), towlower);
    wstring dir_name = zarr_path.substr(0, zarr_path.find_last_of(L"/\\") + 1);

    if (dir_name.size() > 1 && ext == L"zarr")
        dir_name = zarr_path;


    boost::filesystem::path file_path(zarr_path);

    //boost::filesystem::path::imbue(std::locale(std::locale(), new std::codecvt_utf8_utf16<wchar_t>()));
    boost::filesystem::path root_path(dir_name);

    directory_iterator end_itr; // default construction yields past-the-end
    if (output.size() > 0)
        output.clear();

    bool skip = false;
    std::regex pattern(R"((^|\bs)\d+$)");

    if (!exists(dir_name)) {
        slash = L'/';
        auto root_attrpath = root_path / ".zattrs";
        string wxpath = root_attrpath.generic_string();
        if (DownloadFile(wxpath)) {

            bool bioformats = false;
            std::ifstream root_ifs(wxpath);
            if (root_ifs.is_open()) {
                auto jf = json::parse(root_ifs);
                if (!jf.is_null() && jf.contains(BioformatsKey)) {
                    bioformats = true;
                }
            }
            if (bioformats) {
                int ch = 0;
                bool end = false;
                string str_path;
                do {
                    auto ch_attrs_path = root_path / to_string(ch) / ".zattrs";
                    str_path = ch_attrs_path.generic_string();
                    end = DownloadFile(str_path);
                    try {
                        std::ifstream ch_ifs(str_path);
                        if (ch_ifs.is_open()) {
                            auto jf = json::parse(ch_ifs);
                            if (jf.is_null())
                                end = false;
                        }
                    }
                    catch (...) {
                        end = false;
                    }
                    if (end) {
                        auto ch_dir_path = root_path / to_string(ch);
                        output.push_back(ch_dir_path.generic_wstring());
                    }
                    ch++;
                } while (end);

                skip = true;
                slash = L'/';
                dir_name = dir_name + slash;
            }
            else {
                output.push_back(root_path.wstring());
                skip = true;
                slash = L'/';
                dir_name = dir_name + slash;
            }
        }
    }
    
    if (!skip) {
        auto root_attrpath = root_path / ".zattrs";
        std::ifstream root_ifs(root_attrpath.string());
        if (root_ifs.is_open()) {
            auto jf = json::parse(root_ifs);
            if (!jf.is_null() && jf.contains(MultiScalesKey)) {
                output.push_back(root_path.wstring());
                skip = true;
            }
        }
        auto root_arraypath = root_path / ".zarray";
        if (exists(root_arraypath)) {
            output.push_back(root_path.wstring());
            skip = true;
        }
    }

    if (!skip) {
        fs::recursive_directory_iterator ite(root_path.string());
        for (const auto& entry : ite) {
            try {
                if (fs::is_directory(entry.status())) {
                    auto attrpath = entry.path() / ".zattrs";
                    std::ifstream ifs(attrpath.string());
                    if (ifs.is_open()) {
                        auto jf = json::parse(ifs);
                        if (!jf.is_null()) {
                            if (jf.contains(MultiScalesKey)) {
                                output.push_back(entry.path().wstring());
                                ite.disable_recursion_pending();
                                continue;
                            }
                            if (jf.contains(LabelsKey)) {
                                ite.disable_recursion_pending();
                                continue;
                            }
                        }
                    }
                    auto arraypath = entry.path() / ".zarray";
                    if (exists(arraypath)) {
                        output.push_back(entry.path().wstring());
                        ite.disable_recursion_pending();
                        continue;
                    }
                }
            }
            catch (const fs::filesystem_error & ex) {
                std::cerr << "Error: " << ex.what() << std::endl;
            }
        }
    }

    std::sort(output.begin(), output.end());

    return true;
}

bool BRKXMLReader::GetN5ChannelPaths(wstring n5path, vector<wstring> &output)
{
#ifdef _WIN32
    wchar_t slash = L'\\';
#else
    wchar_t slash = L'/';
#endif
    
    size_t ext_pos = n5path.find_last_of(L".");
    wstring ext = n5path.substr(ext_pos+1);
    transform(ext.begin(), ext.end(), ext.begin(), towlower);
    wstring dir_name = n5path.substr(0, n5path.find_last_of(slash)+1);
    
    if (dir_name.size() > 1 && ext == L"n5")
        dir_name = n5path + slash;
    
    
    boost::filesystem::path file_path(n5path);
    
    //boost::filesystem::path::imbue(std::locale(std::locale(), new std::codecvt_utf8_utf16<wchar_t>()));
    boost::filesystem::path root_path(dir_name);
    
    directory_iterator end_itr; // default construction yields past-the-end
    if (output.size() > 0)
        output.clear();
    if (file_path.extension() == ".xml") {
        tinyxml2::XMLDocument xmldoc;
        if (xmldoc.LoadFile(file_path.string().c_str()) != 0){
            return false;
        }
        string r_n5path = ReadBDVFilePath(xmldoc);
        root_path = root_path / r_n5path;
    }
    
    std::regex pattern(R"((^|\bs)\d+$)");
    
    fs::recursive_directory_iterator ite(root_path.string());
    for (const auto& entry : ite) {
        try {
            if (fs::is_directory(entry.status())) {
                if (std::regex_match(entry.path().filename().string(), pattern)) {
                    ite.disable_recursion_pending();
                    continue;
                }
                for (const auto& sub_entry : fs::directory_iterator(entry.path())) {
                    try {
                        if (fs::is_directory(sub_entry.status())) {
                            std::string dir_name = sub_entry.path().filename().string();
                            if (std::regex_match(dir_name, pattern)) {
                                output.push_back(entry.path().wstring());
                                ite.disable_recursion_pending();
                                break;
                            }
                        }
                    }
                    catch (const fs::filesystem_error & ex) {
                        std::cerr << "Error: " << ex.what() << std::endl;
                    }
                }
            }
        }
        catch (const fs::filesystem_error & ex) {
            std::cerr << "Error: " << ex.what() << std::endl;
        }
    }
    
    std::sort(output.begin(), output.end());
    
    return true;
}

DatasetAttributes* BRKXMLReader::parseDatasetMetadataZarr(wstring jpath)
{
    boost::filesystem::path path(jpath);
    std::ifstream ifs(path.string());

    if (!ifs.is_open())
        return nullptr;

    auto jf = json::parse(ifs);

    if (jf.is_null() ||
        !jf.contains(ShapeKey) ||
        !jf.contains(ZarrDataTypeKey) ||
        !jf.contains(ChunksKey))
        return nullptr;

    DatasetAttributes* ret = new DatasetAttributes();

    ret->m_dimensions = jf[ShapeKey].get<vector<long>>();
    reverse(ret->m_dimensions.begin(), ret->m_dimensions.end());

    string str = jf[ZarrDataTypeKey].get<string>();
    ret->m_is_big_endian = (str[0] == '>');

    int bytes = 0;
    bytes = str[2] - '0';
    ret->m_dataType = -1;
    if (str[1] == 'i') {
        if (bytes == 1)
            ret->m_dataType = nrrdTypeChar;
        else if (bytes == 2)
            ret->m_dataType = nrrdTypeShort;
        else if (bytes == 4)
            ret->m_dataType = nrrdTypeInt;
    }
    else if (str[1] == 'u') {
        if (bytes == 1)
            ret->m_dataType = nrrdTypeUChar;
        else if (bytes == 2)
            ret->m_dataType = nrrdTypeUShort;
        else if (bytes == 4)
            ret->m_dataType = nrrdTypeUInt;
    }
    else if (str[1] == 'f') {
        if (bytes == 4)
            ret->m_dataType = nrrdTypeFloat;
        else if (bytes == 8)
            ret->m_dataType = nrrdTypeDouble;
    }


    if (jf.contains(OrderKey)) {
        if (jf[OrderKey] == "C")
            ret->m_is_row_major = true;
        else
            ret->m_is_row_major = false;
    }
    else {
        ret->m_is_row_major = true;
    }

    ret->m_blockSize = jf[ChunksKey].get<vector<int>>();
    reverse(ret->m_blockSize.begin(), ret->m_blockSize.end());

    ret->m_pix_res = vector<double>(3, -1.0);
    
    /* version 0 */
    string cptype;
    if (jf.contains(CompressorKey) && jf[CompressorKey].contains(CompressorIDKey)) {
        cptype = jf[CompressorKey][CompressorIDKey].get<string>();
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
        else if (cptype == "zstd")
            ret->m_compression = 6;
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
                //else if (bcptype == BLOSC_SNAPPY_COMPNAME)
                //    ret->m_blosc_param.ctype = BLOSC_SNAPPY;
                else if (bcptype == BLOSC_ZLIB_COMPNAME)
                    ret->m_blosc_param.ctype = BLOSC_ZLIB;
                else if (bcptype == BLOSC_ZSTD_COMPNAME)
                    ret->m_blosc_param.ctype = BLOSC_ZSTD;
                else
                {
                    m_error_msg = L"Error (N5Reader): Unsupported blosc compression format ";
                    m_error_msg += s2ws(bcptype);
                }
            }
            else ret->m_blosc_param.ctype = BLOSC_BLOSCLZ;

            if (jf[CompressionKey].contains(BloscShuffleKey))
                ret->m_blosc_param.suffle = jf[CompressionKey][BloscShuffleKey].get<int>();
            else ret->m_blosc_param.suffle = 0;
        }
        else
            ret->m_compression = -1;
    }

    switch (ret->m_compression)
    {
    case 0:
        ret->m_compression = BRICK_FILE_TYPE_N5RAW;
        break;
    case 1:
        ret->m_compression = BRICK_FILE_TYPE_N5GZIP;
        break;
    case 3:
        ret->m_compression = BRICK_FILE_TYPE_N5LZ4;
        break;
    case 5:
        ret->m_compression = BRICK_FILE_TYPE_N5BLOSC;
        break;
    case 6:
        ret->m_compression = BRICK_FILE_TYPE_N5ZSTD;
        break;
    default:
        ret->m_compression = BRICK_FILE_TYPE_NONE;
        m_error_msg = L"Error (N5Reader): Unsupported compression format ";
        m_error_msg += s2ws(cptype);
    }

    return ret;
}

DatasetAttributes* BRKXMLReader::parseDatasetMetadataN5(wstring jpath)
{
    //boost::filesystem::path::imbue(std::locale( std::locale(), new std::codecvt_utf8_utf16<wchar_t>()));
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

    ret->m_is_big_endian = true;
    
	string str = jf[DataTypeKey].get<string>();
	if (str == "uint8")
		ret->m_dataType = nrrdTypeUChar;
	else if (str == "uint16")
		ret->m_dataType = nrrdTypeUShort;
    else if (str == "uint32")
        ret->m_dataType = nrrdTypeUInt;
    else if (str == "float32")
        ret->m_dataType = nrrdTypeFloat;
	else
    {
		ret->m_dataType = 0;
        m_error_msg = L"Error (N5Reader): Unsupported data type ";
        m_error_msg += s2ws(str);
    }

    ret->m_is_row_major = true;
    
    ret->m_blockSize = jf[BlockSizeKey].get<vector<int>>();
    
	if (jf.contains(CompressionKey) &&
		(jf[CompressionKey].type() == json::value_t::number_integer || jf[CompressionKey].type() == json::value_t::number_unsigned))
		ret->m_compression = jf[CompressionKey].get<int>();
	if (jf.contains(PixelResolutionKey) && jf[PixelResolutionKey].contains(DimensionsKey))
		ret->m_pix_res = jf[PixelResolutionKey][DimensionsKey].get<vector<double>>();
	else if (jf.contains(TransformKey) && jf[TransformKey].contains(ScaleKey))
        ret->m_pix_res = jf[TransformKey][ScaleKey].get<vector<double>>();
    else
		ret->m_pix_res = vector<double>(3, -1.0);
    
    if (jf.contains(DownsamplingFactorsKey))
        ret->m_downsampling_factors = jf[DownsamplingFactorsKey].get<vector<double>>();
    else 
        ret->m_downsampling_factors = vector<double>(3, -1.0);

    if (jf.contains(TransformKey) && jf[TransformKey].contains(AxesKey))
    {
        vector<string> axis_order = jf[TransformKey][AxesKey].get<vector<string>>();
        std::vector<int> indices(3, -1);
        for (size_t i = 0; i < axis_order.size(); ++i) {
            if (axis_order[i] == "x" || axis_order[i] == "X")
                indices[0] = i;
            else if (axis_order[i] == "y" || axis_order[i] == "Y")
                indices[1] = i;
            else if (axis_order[i] == "z" || axis_order[i] == "Z")
                indices[2] = i;
        }

        if (indices[0] >= 0 && indices[1] >= 0 && indices[2] >= 0)
        {
            vector<double> tmp_pxres = ret->m_pix_res;
            tmp_pxres[0] = ret->m_pix_res[indices[0]];
            tmp_pxres[1] = ret->m_pix_res[indices[1]];
            tmp_pxres[2] = ret->m_pix_res[indices[2]];
            ret->m_pix_res = tmp_pxres;
        }
    }
    
    /* version 0 */
    string cptype;
    if (jf.contains(CompressionKey) && jf[CompressionKey].contains(CompressionTypeKey)) {
        cptype = jf[CompressionKey][CompressionTypeKey].get<string>();
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
                //else if (bcptype == BLOSC_SNAPPY_COMPNAME)
                //    ret->m_blosc_param.ctype = BLOSC_SNAPPY;
                else if (bcptype == BLOSC_ZLIB_COMPNAME)
                    ret->m_blosc_param.ctype = BLOSC_ZLIB;
                else if (bcptype == BLOSC_ZSTD_COMPNAME)
                    ret->m_blosc_param.ctype = BLOSC_ZSTD;
                else
                {
                    m_error_msg = L"Error (N5Reader): Unsupported blosc compression format ";
                    m_error_msg += s2ws(bcptype);
                }
            }
            else ret->m_blosc_param.ctype = BLOSC_BLOSCLZ;
            
            if (jf[CompressionKey].contains(BloscShuffleKey))
                ret->m_blosc_param.suffle = jf[CompressionKey][BloscShuffleKey].get<int>();
            else ret->m_blosc_param.suffle = 0;
        }
        else
            ret->m_compression = -1;
    }

	switch (ret->m_compression)
	{
	case 0:
		ret->m_compression = BRICK_FILE_TYPE_N5RAW;
		break;
	case 1:
		ret->m_compression = BRICK_FILE_TYPE_N5GZIP;
		break;
	case 3:
		ret->m_compression = BRICK_FILE_TYPE_N5LZ4;
		break;
    case 5:
        ret->m_compression = BRICK_FILE_TYPE_N5BLOSC;
        break;
	default:
		ret->m_compression = BRICK_FILE_TYPE_NONE;
        m_error_msg = L"Error (N5Reader): Unsupported compression format ";
        m_error_msg += s2ws(cptype);
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


//https://stackoverflow.com/questions/11921463/find-a-specific-node-in-a-xml-document-with-tinyxml
static bool parseXML(tinyxml2::XMLDocument& xXmlDocument, std::string sSearchString, std::function<void(tinyxml2::XMLNode*)> fFoundSomeElement)
{
    if ( xXmlDocument.ErrorID() != tinyxml2::XML_SUCCESS )
    {
        return false;
    } // if
    
    //ispired by http://stackoverflow.com/questions/11921463/find-a-specific-node-in-a-xml-document-with-tinyxml
    tinyxml2::XMLNode * xElem = xXmlDocument.FirstChild();
    while(xElem)
    {
        if (xElem->Value() && !std::string(xElem->Value()).compare(sSearchString))
        {
            fFoundSomeElement(xElem);
        }
        
        /*
         *   We move through the XML tree following these rules (basically in-order tree walk):
         *
         *   (1) if there is one or more child element(s) visit the first one
         *       else
         *   (2)     if there is one or more next sibling element(s) visit the first one
         *               else
         *   (3)             move to the parent until there is one or more next sibling elements
         *   (4)             if we reach the end break the loop
         */
        if (xElem->FirstChildElement()) //(1)
            xElem = xElem->FirstChildElement();
        else if (xElem->NextSiblingElement())  //(2)
            xElem = xElem->NextSiblingElement();
        else
        {
            while(xElem->Parent() && !xElem->Parent()->NextSiblingElement()) //(3)
                xElem = xElem->Parent();
            if(xElem->Parent() && xElem->Parent()->NextSiblingElement())
                xElem = xElem->Parent()->NextSiblingElement();
            else //(4)
                break;
        }//else
    }//while
    
    return true;
}

string BRKXMLReader::ReadBDVFilePath(tinyxml2::XMLDocument& xXmlDocument)
{
    string rpath;
    
    parseXML(xXmlDocument, "n5",[&rpath](tinyxml2::XMLNode* xElem)
             {
                 tinyxml2::XMLElement *elem = xElem->ToElement();
                 if (elem && elem->Attribute("type") && elem->GetText())
                 {
                     string str;
                     str = elem->Attribute("type");
                     if (str.compare("relative") == 0)
                     {
                         const char* txt = elem->GetText();
                         if (txt)
                             rpath = elem->GetText();
                     }
                 }
             });
    
    return rpath;
}

void BRKXMLReader::ReadBDVResolutions(tinyxml2::XMLDocument& xXmlDocument, vector<vector<double>> &resolutions)
{
    string rpath;
    
    parseXML(xXmlDocument, "ViewSetup",[&resolutions](tinyxml2::XMLNode* xElem)
             {
                 int id = -1;
                 tinyxml2::XMLElement *elem = xElem->ToElement();
                 if (elem)
                 {
                     tinyxml2::XMLElement *child = elem->FirstChildElement();
                     while (child)
                     {
                         if (child->Name() && strcmp(child->Name(), "id") == 0)
                         {
                             const char* txt = child->GetText();
                             if (txt)
                             {
                                 id = STOI(txt);
                                 if (id >= resolutions.size())
                                     resolutions.resize(id+1);
                                 break;
                             }
                         }
                         child = child->NextSiblingElement();
                     }
                     
                     if (id < 0 || id >= resolutions.size())
                         return;
                     
                     child = elem->FirstChildElement();
                     while (child)
                     {
                         if (strcmp(child->Name(), "voxelSize") == 0)
                         {
                             tinyxml2::XMLElement *child2 = child->FirstChildElement();
                             while (child2)
                             {
                                 if (child2->Name())
                                 {
                                     if (strcmp(child2->Name(), "size") == 0)
                                     {
                                         const char* txt = child2->GetText();
                                         if (txt)
                                         {
                                             string str = txt;
                                             string space_delimiter = " ";
                                             size_t pos = 0;
                                             while ((pos = str.find(space_delimiter)) != string::npos) {
                                                 resolutions[id].push_back(STOD(str.substr(0, pos).c_str()));
                                                 str.erase(0, pos + space_delimiter.length());
                                             }
                                             resolutions[id].push_back(STOD(str.c_str()));
                                             break;
                                         }
                                     }
                                 }
                                 child2 = child2->NextSiblingElement();
                             }
                             break;
                         }
                         child = child->NextSiblingElement();
                     }
                 }
             });
}

void BRKXMLReader::ReadBDVViewRegistrations(tinyxml2::XMLDocument& xXmlDocument, vector<vector<FLIVR::Transform>> &transforms)
{
    string rpath;
    
    vector<vector<double>> setup_dimemsions;
    
    parseXML(xXmlDocument, "ViewSetup",[&setup_dimemsions](tinyxml2::XMLNode* xElem)
             {
                 int id = -1;
                 tinyxml2::XMLElement *elem = xElem->ToElement();
                 if (elem)
                 {
                     tinyxml2::XMLElement *child = elem->FirstChildElement();
                     while (child)
                     {
                         if (child->Name() && strcmp(child->Name(), "id") == 0)
                         {
                             const char* txt = child->GetText();
                             if (txt)
                             {
                                 id = STOI(txt);
                                 if (id >= setup_dimemsions.size())
                                     setup_dimemsions.resize(id+1);
                                 break;
                             }
                         }
                         child = child->NextSiblingElement();
                     }
                     
                     if (id < 0 || id >= setup_dimemsions.size())
                         return;
                     
                     child = elem->FirstChildElement();
                     while (child)
                     {
                         if (strcmp(child->Name(), "size") == 0)
                         {
                             const char* txt = child->GetText();
                             if (txt)
                             {
                                 string str = txt;
                                 string space_delimiter = " ";
                                 size_t pos = 0;
                                 while ((pos = str.find(space_delimiter)) != string::npos) {
                                     setup_dimemsions[id].push_back(STOD(str.substr(0, pos).c_str()));
                                     str.erase(0, pos + space_delimiter.length());
                                 }
                                 setup_dimemsions[id].push_back(STOD(str.c_str()));
                                 break;
                             }
                             break;
                         }
                         child = child->NextSiblingElement();
                     }
                 }
             });
    
    
    parseXML(xXmlDocument, "ViewRegistration",[setup_dimemsions, &transforms](tinyxml2::XMLNode* xElem)
             {
                 int timepoint, setup;
                 tinyxml2::XMLElement *elem = xElem->ToElement();
                 if (elem && elem->Attribute("timepoint") && elem->Attribute("setup"))
                 {
                     timepoint = STOI(elem->Attribute("timepoint"));
                     setup = STOI(elem->Attribute("setup"));
                     if (timepoint >= transforms.size())
                         transforms.resize(timepoint+1);
                     if (setup >= transforms[timepoint].size())
                         transforms[timepoint].resize(setup+1);
                     vector<string> strmats;
                     string strcalib;
                     tinyxml2::XMLElement *child = elem->FirstChildElement();
                     while (child)
                     {
                         if (child->Name() && strcmp(child->Name(), "ViewTransform") == 0 && child->Attribute("type"))
                         {
                             if (strcmp(child->Attribute("type"), "affine") == 0)
                             {
                                 bool calib = false;
                                 tinyxml2::XMLElement *child2 = child->FirstChildElement();
                                 while (child2)
                                 {
                                     if (child2->Name())
                                     {
                                         if (strcmp(child2->Name(), "Name") == 0 && child2->GetText() && strcmp(child2->GetText(), "calibration") == 0)
                                         {
                                             calib = true;
                                         }
                                         else if (strcmp(child2->Name(), "affine") == 0 )
                                         {
                                             const char* txt = child2->GetText();
                                             if (txt)
                                                 strmats.push_back(txt);
                                         }
                                     }
                                     child2 = child2->NextSiblingElement();
                                 }
                                 if (calib)
                                 {
                                     strcalib = strmats.back();
                                     strmats.pop_back();
                                 }
                             }
                         }
                         child = child->NextSiblingElement();
                     }
                     
                     FLIVR::Transform view_tform;
                     view_tform.load_identity();
                     string space_delimiter = " ";
                     
                     double dim_x = 1.0;
                     double dim_y = 1.0;
                     double dim_z = 1.0;
                     if (!strcalib.empty())
                     {
                         vector<double> calib;
                         size_t pos = 0;
                         while ((pos = strcalib.find(space_delimiter)) != string::npos) {
                             calib.push_back(STOD(strcalib.substr(0, pos).c_str()));
                             strcalib.erase(0, pos + space_delimiter.length());
                         }
                         calib.push_back(STOD(strcalib.c_str()));
                         if (calib.size() == 12)
                         {
                             dim_x *= calib[0];
                             dim_y *= calib[5];
                             dim_z *= calib[10];
                         }
                     }
                     if (setup_dimemsions.size() > setup && setup_dimemsions[setup].size() >= 3)
                     {
                         dim_x *= setup_dimemsions[setup][0];
                         dim_y *= setup_dimemsions[setup][1];
                         dim_z *= setup_dimemsions[setup][2];
                     }
                     
                     for (auto &text : strmats)
                     {
                         vector<double> mat;
                         size_t pos = 0;
                         while ((pos = text.find(space_delimiter)) != string::npos) {
                             mat.push_back(STOD(text.substr(0, pos).c_str()));
                             text.erase(0, pos + space_delimiter.length());
                         }
                         mat.push_back(STOD(text.c_str()));
                         
                         //mat[0] = 1.0; mat[1] = 0.0; mat[2]  = 0.0;
                         //mat[4] = 0.0; mat[5] = 0.70710678118; mat[6]  = -0.70710678118;
                         //mat[8] = 0.0; mat[9] = 0.70710678118; mat[10] = 0.70710678118;
                         
                         //mat[0] = 1.0; mat[1] = 0.0; mat[2]  = 0.0;
                         //mat[4] = 0.0; mat[5] = 1.0; mat[6]  = 0.0;
                         //mat[8] = 0.0; mat[9] = 0.0; mat[10] = 1.0;
                         
                         //mat[3] = 0.0; mat[7] = 0.0; mat[11] = 0.0;
                         
                         mat.push_back(0.0); mat.push_back(0.0); mat.push_back(0.0); mat.push_back(1.0);
                         if (mat.size() == 16)
                         {
                             mat[3] /= dim_x; mat[7] /= dim_y; mat[11] /= dim_z;
                             
                             FLIVR::Transform tform;
                             tform.set(mat.data());
                             //view_tform.pre_trans(tform);
                             view_tform.set(mat.data());
                         }
                         
                         //break;
                     }
                     
                     transforms[timepoint][setup] = view_tform;
                 }
             });
}

FLIVR::Transform BRKXMLReader::GetBDVTransform(int setup, int timepoint)
{
    FLIVR::Transform ret;
    ret.load_identity();
    
    if (setup < 0)
        setup = m_bdv_setup_id;
    if (timepoint < 0)
        timepoint = m_cur_time;
    
    if (timepoint >= 0 && timepoint < m_bdv_view_transforms.size())
    {
        if (setup >= 0 && setup < m_bdv_view_transforms[timepoint].size())
        {
            return m_bdv_view_transforms[timepoint][setup];
        }
    }
    
    return ret;
}
