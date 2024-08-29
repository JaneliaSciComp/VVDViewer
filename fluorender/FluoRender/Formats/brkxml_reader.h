#ifndef _BRKXML_READER_H_
#define _BRKXML_READER_H_

#include <vector>
#include <base_reader.h>
#include <FLIVR/TextureBrick.h>
#include <FLIVR/Transform.h>
#include <tinyxml2.h>
#include <memory>

#include <json.hpp>

#include <blosc2.h>

#define N5DataTypeChar 0
#define N5DataTypeUChar 1
#define N5DataTypeShort 2
#define N5DataTypeUShort 3
#define N5DataTypeInt 4
#define N5DataTypeUInt 5
#define N5DataTypeFloat 6

#define SerialVersionUID -4521467080388947553L
#define DimensionsKey "dimensions"
#define BlockSizeKey "blockSize"
#define DataTypeKey "dataType"
#define CompressionKey "compression"
#define CompressionTypeKey "type"
#define PixelResolutionKey "pixelResolution"
#define DownsamplingFactorsKey "downsamplingFactors"
#define TransformKey "transform"
#define ScaleKey "scale"

#define BloscLevelKey "clevel"
#define BloscBlockSizeKey "blockSize"
#define BloscCompressionKey "cname"
#define BloscShuffleKey "shuffle"

using namespace std;

class DataBlock
{
public:
    
    int m_type;
    void* m_data;
    
    DataBlock() {
        m_data = NULL; m_type = 0;
    }
    DataBlock(void* data, int type) {
        m_data = data; m_type = type;
    }
    ~DataBlock() {
        if (m_data)
        {
            switch (m_type)
            {
                case N5DataTypeChar:
                case N5DataTypeUChar:
                    delete[] static_cast<unsigned char*>(m_data);
                    break;
                case N5DataTypeShort:
                case N5DataTypeUShort:
                    delete[] static_cast<unsigned short*>(m_data);
                    break;
                case N5DataTypeInt:
                case N5DataTypeUInt:
                    delete[] static_cast<unsigned int*>(m_data);
                    break;
                case N5DataTypeFloat:
                    delete[] static_cast<float*>(m_data);
                    break;
                default:
                    delete[] m_data;
            }
            m_data = nullptr;
        }
    }
};

class DatasetAttributes
{
public:
    
    vector<long> m_dimensions;
    vector<int> m_blockSize;
    int m_dataType;
    int m_compression;
	vector<double> m_pix_res;
	vector<double> m_downsampling_factors;
    
    struct BloscParam {
        int blocksize;
        int clevel;
        int ctype;
        int suffle;
    };
    
    BloscParam m_blosc_param;

	DatasetAttributes()
	{
	}
    
    DatasetAttributes(const vector<long>& dimensions, const vector<int>& blockSize, int dataType, int compression, const vector<double>& pix_res, const vector<double>& downsampling_factors, BloscParam bp)
    {
        m_dimensions = dimensions;
        m_blockSize = blockSize;
        m_dataType = dataType;
        m_compression = compression;
		m_pix_res = pix_res;
        m_blosc_param = bp;
		m_downsampling_factors = downsampling_factors;
    }
    
    vector<long> getDimensions() {
        
        return m_dimensions;
    }
    
    int getNumDimensions() {
        
        return m_dimensions.size();
    }
    
    vector<int> getBlockSize() {
        
        return m_blockSize;
    }
    
    int getCompression() {
        
        return m_compression;
    }
    
    int getDataType() {
        
        return m_dataType;
    }
    
    BloscParam getBloscParam() {
        return m_blosc_param;
    }
    /*
     HashMap<String, Object> asMap() {
     
     final HashMap<String, Object> map = new HashMap<>();
     map.put(dimensionsKey, dimensions);
     map.put(blockSizeKey, blockSize);
     map.put(dataTypeKey, dataType.toString());
     map.put(compressionKey, compression);
     return map;
     }
     */
};

class EXPORT_API BRKXMLReader : public BaseReader
{
public:
	BRKXMLReader();
	~BRKXMLReader();

	void SetFile(string &file);
	void SetFile(wstring &file);
	void SetDir(string &dir);
	void SetDir(wstring &dir);
	void SetSliceSeq(bool ss);
	bool GetSliceSeq();
	void SetTimeSeq(bool ss);
	bool GetTimeSeq();
	void SetTimeId(wstring &id);
	void SetCurTime(int t);
	void SetCurChan(int c);
	wstring GetTimeId();
	void Preprocess();
	void SetBatch(bool batch);
	int LoadBatch(int index);
	int LoadOffset(int offset);
	Nrrd* Convert_ThreadSafe(int t, int c, bool get_max) { return NULL; }
	wstring GetCurName(int t, int c);

	wstring GetPathName() {return m_path_name;}
	wstring GetDataName() {return m_data_name;}
	int GetTimeNum() {return m_time_num;}
	int GetCurTime() {return m_cur_time;}
	int GetChanNum() {return m_chan_num;}
	int GetCurChan() {return m_cur_chan;}
	double GetExcitationWavelength(int chan);
	int GetSliceNum() {return m_slice_num;}
	int GetXSize() {return m_x_size;}
	int GetYSize() {return m_y_size;}
	bool IsSpcInfoValid() {return m_valid_spc;}
	double GetXSpc() {return m_xspc;}
	double GetYSpc() {return m_yspc;}
	double GetZSpc() {return m_zspc;}
	double GetMaxValue() {return m_max_value;}
	double GetScalarScale() {return m_scalar_scale;}
	bool GetBatch() {return m_batch;}//not base_reader
	int GetBatchNum() {return (int)m_batch_list.size();}
	int GetCurBatch() {return m_cur_batch;}
	bool isURL() {return m_isURL;}
	wstring GetExMetadataURL() {return m_ex_metadata_url;}
	void SetInfo();

	FLIVR::FileLocInfo* GetBrickFilePath(int fr, int ch, int id, int lv = -1);
	wstring GetBrickFileName(int fr, int ch, int id, int lv = -1);
	int GetFileType(int lv = -1);

	int GetLevelNum() {return m_level_num;}
	void SetLevel(int lv);
	int GetCopyableLevel() {return m_copy_lv;}
    int GetMaskLevel() {return m_mask_lv;}

	void build_bricks(vector<FLIVR::TextureBrick*> &tbrks, int lv = -1);
	void build_pyramid(vector<FLIVR::Pyramid_Level> &pyramid, vector<vector<vector<vector<FLIVR::FileLocInfo *>>>> &filenames, int t, int c);
	void OutputInfo();

	void GetLandmark(int index, wstring &name, double &x, double &y, double &z, double &spcx, double &spcy, double &spcz);
	int GetLandmarkNum() {return m_landmarks.size();}
	wstring GetROITree() {return m_roi_tree;}
	void GetMetadataID(wstring &mid) {mid = m_metadata_id;}

	bool loadMetadata(const wstring &file = wstring());
	void LoadROITree(tinyxml2::XMLElement *lvNode);
	void LoadROITree_r(tinyxml2::XMLElement *lvNode, wstring& tree, const wstring& parent, int& gid);

	tinyxml2::XMLDocument *GetVVDXMLDoc() {return &m_doc;}
	tinyxml2::XMLDocument *GetMetadataXMLDoc() {return &m_md_doc;}
    
	void loadFSN5();
    void SetBDVMetadataPath(const wstring path) { m_bdv_metadata_path = path; }
	DatasetAttributes* parseDatasetMetadata(wstring jpath);
    map<wstring, wstring> getAttributes(wstring pathName);
    DataBlock readBlock(wstring pathName, const DatasetAttributes& datasetAttributes, const vector<long> gridPosition);
    vector<wstring> list(wstring pathName);
    wstring getDataBlockPath(wstring datasetPathName, const vector<long>& gridPosition);
    wstring getAttributesPath(wstring pathName);
    wstring removeLeadingSlash(const wstring pathName);
    
    wstring getChannelName(int i)
    {
        wstring ret;
        if (i >= 0 && i < m_chan_names.size())
            ret = m_chan_names[i];
        return ret;
    }
    
    FLIVR::Transform GetBDVTransform(int setup = -1, int timepoint = -1);
    
    static bool GetN5ChannelPaths(wstring n5path, vector<wstring> &output);

protected:
	Nrrd* ConvertNrrd(int t, int c, bool get_max);

private:
	wstring m_path_name;
	wstring m_data_name;
	wstring m_dir_name;
	wstring m_url_dir;
	wstring m_ex_metadata_path;
	wstring m_ex_metadata_url;
	wstring m_roi_tree;

	struct BrickInfo
	{
		//index
		int id;
		//size
		int x_size;
		int y_size;
		int z_size;
		//start position
		int x_start;
		int y_start;
		int z_start;
		//offset to brick
		long long offset;
		long long fsize;
		//tbox
		double tx0, ty0, tz0, tx1, ty1, tz1;
		//bbox
		double bx0, by0, bz0, bx1, by1, bz1;
	};
	struct LevelInfo
	{
		int imageW;
		int imageH;
		int imageD;
		double xspc;
		double yspc;
		double zspc;
		int brick_baseW;
		int brick_baseH;
		int brick_baseD;
		int bit_depth;
		int file_type;
		vector<vector<vector<FLIVR::FileLocInfo *>>> filename;//Frame->Channel->BrickID->Filename
		vector<BrickInfo *> bricks;
        
        int blosc_blocksize;
        int blosc_clevel;
        int blosc_ctype;
        int blosc_suffle;
	};
	vector<LevelInfo> m_pyramid;

	
	struct ImageInfo
	{
		int nChannel;
		int nFrame;
		int nLevel;
		int copyableLv;
        int maskLv;
	};
	ImageInfo m_imageinfo;
    
    vector<vector<double>> m_bdv_resolutions;
    vector<vector<FLIVR::Transform>> m_bdv_view_transforms;

	int m_file_type;

	int m_level_num;
	int m_cur_level;
	int m_copy_lv;
    int m_mask_lv;
	
	int m_time_num;
	int m_cur_time;
	int m_chan_num;
	int m_cur_chan;

	//3d batch
	bool m_batch;
	vector<wstring> m_batch_list;
	int m_cur_batch;

	int m_slice_num;
	int m_x_size;
	int m_y_size;
	bool m_valid_spc;
	double m_xspc;
	double m_yspc;
	double m_zspc;
	double m_max_value;
	double m_scalar_scale;

	bool m_isURL;

	//time sequence id
	wstring m_time_id;

	tinyxml2::XMLDocument m_doc;
	tinyxml2::XMLDocument m_md_doc;

	struct Landmark
	{
		wstring name;
		double x, y, z;
		double spcx, spcy, spcz;
	};
	vector<Landmark> m_landmarks;
	wstring m_metadata_id;
    
    vector<wstring> m_chan_names;
    
    wstring m_bdv_metadata_path;
    int m_bdv_setup_id;

private:
	ImageInfo ReadImageInfo(tinyxml2::XMLElement *seqNode);
	void ReadBrick(tinyxml2::XMLElement *brickNode, BrickInfo &binfo);
	void ReadLevel(tinyxml2::XMLElement* lvNode, LevelInfo &lvinfo);
	void ReadFilenames(tinyxml2::XMLElement* fileRootNode, vector<vector<vector<FLIVR::FileLocInfo *>>> &filename);
	void ReadPackedBricks(tinyxml2::XMLElement* packNode, vector<BrickInfo *> &brks);
	void Readbox(tinyxml2::XMLElement *boxNode, double &x0, double &y0, double &z0, double &x1, double &y1, double &z1);
	void ReadPyramid(tinyxml2::XMLElement *lvRootNode, vector<LevelInfo> &pylamid);
    
    static string ReadBDVFilePath(tinyxml2::XMLDocument& xXmlDocument);
    void ReadBDVResolutions(tinyxml2::XMLDocument& xXmlDocument, vector<vector<double>> &resolutions);
    void ReadBDVViewRegistrations(tinyxml2::XMLDocument& xXmlDocument, vector<vector<FLIVR::Transform>> &transforms);
    void ReadResolutionPyramidFromSingleN5Dataset(wstring root_dir, int f, int c, vector<double> pix_res);

	void Clear();
};

#endif//_BRKXML_READER_H_
