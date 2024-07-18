//  
//  For more information, please see: http://software.sci.utah.edu
//  
//  The MIT License
//  
//  Copyright (c) 2004 Scientific Computing and Imaging Institute,
//  University of Utah.
//  
//  
//  Permission is hereby granted, free of charge, to any person obtaining a
//  copy of this software and associated documentation files (the "Software"),
//  to deal in the Software without restriction, including without limitation
//  the rights to use, copy, modify, merge, publish, distribute, sublicense,
//  and/or sell copies of the Software, and to permit persons to whom the
//  Software is furnished to do so, subject to the following conditions:
//  
//  The above copyright notice and this permission notice shall be included
//  in all copies or substantial portions of the Software.
//  
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
//  OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
//  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
//  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
//  DEALINGS IN THE SOFTWARE.
//  

#ifndef SLIVR_TextureBrick_h
#define SLIVR_TextureBrick_h


#include "Ray.h"
#include "BBox.h"
#include "Plane.h"

#ifndef _UNIT_TEST_VOLUME_RENDERER_
#include <wx/thread.h>
#include <curl/curl.h>
#endif

#include <vector>
#include <fstream>
#include <string>
#include <nrrd.h>
#include <stdint.h>
#include <map>
#include <list>
#include <memory>
#include <sstream>

#include <vulkan/vulkan.h>

#include "DLLExport.h"

namespace FLIVR {

	using std::vector;

	class VolumeRenderer;

	// We use no more than 2 texture units.
	// GL_MAX_TEXTURE_UNITS is the actual maximum.
	//we now added maximum to 5
	//which can include mask volumes
#define TEXTURE_MAX_COMPONENTS	5
	//these are the render modes used to determine if each mode is drawn
#define TEXTURE_RENDER_MODES	6
#define TEXTURE_RENDER_MODE_MASK	4
#define TEXTURE_RENDER_MODE_LABEL	5

#define BRICK_FILE_TYPE_NONE	0
#define BRICK_FILE_TYPE_RAW		1
#define BRICK_FILE_TYPE_N5RAW   2
#define BRICK_FILE_TYPE_JPEG	3
#define BRICK_FILE_TYPE_ZLIB	4
#define BRICK_FILE_TYPE_H265	5
#define BRICK_FILE_TYPE_N5GZIP	6
#define BRICK_FILE_TYPE_LZ4		7
#define BRICK_FILE_TYPE_N5LZ4   8
#define BRICK_FILE_TYPE_N5BLOSC 9


	class EXPORT_API VL_Nrrd
	{
		Nrrd* m_nrrd;
		bool m_protected;

	public:
		VL_Nrrd() {
			m_nrrd = NULL; m_protected = false;
		}
		VL_Nrrd(Nrrd* nrrd, bool protection = false) {
			m_nrrd = nrrd; m_protected = protection;
		}
		~VL_Nrrd() {
			if (m_nrrd) {
				if (m_nrrd->data)
				{
					switch (m_nrrd->type)
					{
					case nrrdTypeChar:
					case nrrdTypeUChar:
						delete[] static_cast<unsigned char*>(m_nrrd->data);
						break;
					case nrrdTypeShort:
					case nrrdTypeUShort:
						delete[] static_cast<unsigned short*>(m_nrrd->data);
						break;
					case nrrdTypeInt:
					case nrrdTypeUInt:
						delete[] static_cast<unsigned int*>(m_nrrd->data);
						break;
					case nrrdTypeFloat:
						delete[] static_cast<float*>(m_nrrd->data);
						break;
					default:
						delete[] m_nrrd->data;
					}
					m_nrrd->data = nullptr;
				}
				nrrdNix(m_nrrd);
			}
		}

		int getBytesPerSample() {
			int ret = 0;
			if (m_nrrd) {
				if (m_nrrd->data)
				{
					switch (m_nrrd->type)
					{
					case nrrdTypeChar:
					case nrrdTypeUChar:
						ret = 1;
						break;
					case nrrdTypeShort:
					case nrrdTypeUShort:
						ret = 2;
						break;
					case nrrdTypeInt:
					case nrrdTypeUInt:
					case nrrdTypeFloat:
						ret = 4;
						break;
					default:
						ret = 0;
					}
				}
			}
			return ret;
		}

		Nrrd* getNrrd() { return m_nrrd; }

		Nrrd* getNrrdDeepCopy()
		{
			Nrrd* output = nrrdNew();
			void* indata = m_nrrd->data;
			m_nrrd->data = NULL;
			nrrdCopy(output, m_nrrd);
			m_nrrd->data = indata;

			size_t dim = m_nrrd->dim;
			std::vector<int> size(dim);

			int offset = 0;
			if (dim > 3) offset = 1;

			for (size_t p = 0; p < dim; p++)
				size[p] = (int)m_nrrd->axis[p + offset].size;

			size_t width = size[0];
			size_t height = size[1];
			size_t depth = size[2];

			size_t voxelnum = width * height * depth;

			switch (m_nrrd->type)
			{
			case nrrdTypeChar:
			case nrrdTypeUChar:
				output->data = new unsigned char[voxelnum];
				break;
			case nrrdTypeShort:
			case nrrdTypeUShort:
				output->data = new unsigned short[voxelnum];
				break;
			case nrrdTypeInt:
			case nrrdTypeUInt:
				output->data = new unsigned int[voxelnum];
				break;
			case nrrdTypeFloat:
				output->data = new float[voxelnum];
				break;
			default:
				nrrdNix(output);
				output = nullptr;
			}

			if (output)
				memcpy(output->data, indata, size_t(voxelnum * getBytesPerSample()));

			return output;
		}

		bool isProtected() { return m_protected; }
		void SetProtection(bool protection) { m_protected = protection; }
		size_t getDatasize()
		{
			if (!m_nrrd)
				return 0;
			size_t b = getBytesPerSample();
			size_t size[3];
			int offset = 0;
			if (m_nrrd->dim > 3) offset = 1;

			for (size_t p = 0; p < 3; p++)
				size[p] = (int)m_nrrd->axis[p + offset].size;
			return size[0] * size[1] * size[2] * b;
		}

		void getDimensions(int &w, int &h, int &d)
		{
			if (!m_nrrd)
				return;
			size_t size[3] = {};
			int offset = 0;
			if (m_nrrd->dim > 3) offset = 1;
			for (size_t p = 0; p < 3 && p+offset < m_nrrd->dim; p++)
				size[p] = (int)m_nrrd->axis[p + offset].size;
			
			w = size[0];
			h = size[1];
			d = size[2];

			return;
		}
        
        //you must check null before using this
        float getFloatUnsafe(size_t idx)
        {
            float ret = 0.0f;
            switch (m_nrrd->type)
            {
            case nrrdTypeChar:
            case nrrdTypeUChar:
                ret = (float)( ((unsigned char *)m_nrrd->data)[idx] );
                break;
            case nrrdTypeShort:
            case nrrdTypeUShort:
                ret = (float)( ((unsigned short *)m_nrrd->data)[idx] );
                break;
            case nrrdTypeInt:
            case nrrdTypeUInt:
                    ret = (float)( ((unsigned int *)m_nrrd->data)[idx] );
                break;
            case nrrdTypeFloat:
                ret = ((float *)m_nrrd->data)[idx];
                break;
            }
            
            return ret;
        }

		//you must check null before using this
		void setFloatUnsafe(size_t idx, float value)
		{
			float ret = 0.0f;
			switch (m_nrrd->type)
			{
			case nrrdTypeChar:
			case nrrdTypeUChar:
				((unsigned char*)m_nrrd->data)[idx] = (unsigned char) value;
				break;
			case nrrdTypeShort:
			case nrrdTypeUShort:
				((unsigned short*)m_nrrd->data)[idx] = (unsigned short) value;
				break;
			case nrrdTypeInt:
			case nrrdTypeUInt:
				((unsigned int*)m_nrrd->data)[idx] = (unsigned int) value;
				break;
			case nrrdTypeFloat:
				((float*)m_nrrd->data)[idx] = value;
				break;
			}
		}


	};

	class EXPORT_API VL_Array
	{
		char *m_data;
		size_t m_size;
		bool m_protected;

	public:
		VL_Array() {
			m_data = NULL; m_protected = false; m_size = 0;
		}
		VL_Array(char *data, size_t size, bool protection = false) {
			m_data = data; m_size = size; m_protected = protection;
		}
		~VL_Array() {
			if (m_data) {
				delete [] m_data;
				m_data = NULL;
			}
		}
		void *getData() { return m_data; }
		size_t getSize() { return m_size; }
		bool isProtected() { return m_protected; }
		void SetProtection(bool protection) { m_protected = protection; }
	};
	
	class EXPORT_API FileLocInfo {
	public:
		FileLocInfo()
		{
			filename = L"";
			offset = 0;
			datasize = 0;
			type = 0;
			isurl = false;
			cached = false;
			cache_filename = L"";
			id_string = L"";
            blosc_blocksize_x = 0;
            blosc_blocksize_y = 0;
            blosc_blocksize_z = 0;
            blosc_clevel = 0;
            blosc_ctype = 0;
            blosc_suffle = 0;
            isn5 = false;
            isvalid = true;
		}
		FileLocInfo(std::wstring filename_, int offset_, int datasize_, int type_, bool isurl_, bool isn5_, int bblocksize_x_ = 0, int bblocksize_y_ = 0, int bblocksize_z_ = 0, int bclevel_ = 0, int bctype_ = 0, int bsuffle_ = 0)
		{
			filename = filename_;
			offset = offset_;
			datasize = datasize_;
			type = type_;
			isurl = isurl_;
			cached = false;
			cache_filename = L"";
			std::wstringstream wss;
			wss << filename << L" " << offset;
			id_string = wss.str();
            isn5 = isn5_;
            blosc_blocksize_x = bblocksize_x_;
            blosc_blocksize_y = bblocksize_y_;
            blosc_blocksize_z = bblocksize_z_;
            blosc_clevel = bclevel_;
            blosc_ctype = bctype_;
            blosc_suffle = bsuffle_;
            isvalid = true;
		}
		FileLocInfo(const FileLocInfo &copy)
		{
			filename = copy.filename;
			offset = copy.offset;
			datasize = copy.datasize;
			type = copy.type;
			isurl = copy.isurl;
			cached = copy.cached;
			cache_filename = copy.cache_filename;
			id_string = copy.id_string;
            isn5 = copy.isn5;
            blosc_blocksize_x = copy.blosc_blocksize_x;
            blosc_blocksize_y = copy.blosc_blocksize_y;
            blosc_blocksize_z = copy.blosc_blocksize_z;
            blosc_clevel = copy.blosc_clevel;
            blosc_ctype = copy.blosc_ctype;
            blosc_suffle = copy.blosc_suffle;
            isvalid = copy.isvalid;
		}

		std::wstring filename;
		long long offset;
		long long datasize;
		int type; //1-raw; 2-jpeg; 3-zlib;
		bool isurl;
		bool cached;
		std::wstring cache_filename;
		std::wstring id_string;
        
        bool isn5;
        int blosc_blocksize_x;
        int blosc_blocksize_y;
        int blosc_blocksize_z;
        int blosc_clevel;
        int blosc_ctype;
        int blosc_suffle;
        
        bool isvalid;
	};

	class EXPORT_API MemCache {
	public:
		MemCache()
		{
			data = NULL;
			datasize = 0;
		}
		MemCache(char *d, size_t size)
		{
			data = d;
			datasize = size;
		}
		~MemCache()
		{
			if (data) delete [] data;
		}
		char *data;
		size_t datasize;
	};

	class EXPORT_API TextureBrick
	{
	public:
		enum CompType
		{
			TYPE_NONE=0, TYPE_INT, TYPE_INT_GRAD, TYPE_GM, TYPE_MASK, TYPE_LABEL, TYPE_STROKE
		};
		// Creator of the brick owns the nrrd memory.
		TextureBrick(const std::shared_ptr<VL_Nrrd>& n0, const std::shared_ptr<VL_Nrrd>& n1,
			int nx, int ny, int nz, int nc, int* nb, int ox, int oy, int oz,
			int mx, int my, int mz, const BBox& bbox, const BBox& tbox, const BBox& dbox, int findex = 0, long long offset = 0LL, long long fsize = 0LL);
		virtual ~TextureBrick();

		inline BBox &bbox() { return bbox_; }
		inline BBox &tbox() { return tbox_; }
		inline BBox &dbox() { return dbox_; }

		inline int nx() { return nx_; }
		inline int ny() { return ny_; }
		inline int nz() { return nz_; }
		void nx(int nx) { nx_ = nx; }
		void ny(int ny) { ny_ = ny; }
		void nz(int nz) { nz_ = nz; }

		inline int nc() { return nc_; }
		inline int nb(int c)
		{
			assert(c >= 0 && c < TEXTURE_MAX_COMPONENTS);
			return nb_[c];
		}
		void nb(int n, int c)
		{
			assert(c >= 0 && c < TEXTURE_MAX_COMPONENTS);
			nb_[c] = n;
		}
		inline void nmask(int mask) { nmask_ = mask; }
		inline int nmask() { return nmask_; }
		inline void nlabel(int label) {nlabel_ = label;}
		inline int nlabel() {return nlabel_;}
		inline void nstroke(int stroke) {nstroke_ = stroke;}
		inline int nstroke() {return nstroke_;}
		inline void ntype(CompType type, int c)
		{
			assert(c >= 0 && c < TEXTURE_MAX_COMPONENTS);
			ntype_[c] = type;
		}
		inline CompType ntype(int c)
		{
			assert(c >= 0 && c < TEXTURE_MAX_COMPONENTS);
			return ntype_[c];
		}

		inline int mx() { return mx_; }
		inline int my() { return my_; }
		inline int mz() { return mz_; }
		void mx(int mx) { mx_ = mx; }
		void my(int my) { my_ = my; }
		void mz(int mz) { mz_ = mz; }

		inline int ox() { return ox_; }
		inline int oy() { return oy_; }
		inline int oz() { return oz_; }
		void ox(int ox) { ox_ = ox; }
		void oy(int oy) { oy_ = oy; }
		void oz(int oz) { oz_ = oz; }

		virtual int sx();
		virtual int sy();
		virtual int sz();

		inline void set_drawn(int mode, bool val)
		{ if (mode>=0 && mode<TEXTURE_RENDER_MODES) drawn_[mode] = val; }
		inline void set_drawn(bool val)
		{ for (int i=0; i<TEXTURE_RENDER_MODES; i++) drawn_[i] = val; }
		inline bool drawn(int mode)
		{ if (mode>=0 && mode<TEXTURE_RENDER_MODES) return drawn_[mode]; else return false;}

		inline void set_dirty(int comp, bool val)
		{ if (comp>=0 && comp<TEXTURE_MAX_COMPONENTS) dirty_[comp] = val; }
		inline void set_dirty(bool val)
		{ for (int i=0; i<TEXTURE_MAX_COMPONENTS; i++) dirty_[i] = val; }
		inline bool dirty(int comp)
		{ if (comp>=0 && comp<TEXTURE_MAX_COMPONENTS) return dirty_[comp]; else return false;}
        
        inline void set_skip(int comp, bool val)
        { if (comp>=0 && comp<TEXTURE_MAX_COMPONENTS) skip_[comp] = val; }
        inline void set_skip(bool val)
        { for (int i=0; i<TEXTURE_MAX_COMPONENTS; i++) skip_[i] = val; }
        inline bool skip(int comp)
        { if (comp>=0 && comp<TEXTURE_MAX_COMPONENTS) return skip_[comp]; else return false;}
        
        inline void set_modified(int comp, bool val)
        {
            if (comp>=0 && comp<TEXTURE_MAX_COMPONENTS)
            {
                modified_[comp] = val;
                if (val)
                    skip_[comp] = false;
            }
        }
        inline void set_modified(bool val)
        {
            for (int i=0; i<TEXTURE_MAX_COMPONENTS; i++)
            {
                modified_[i] = val;
                if (val)
                    skip_[i] = false;
            }
        }
        inline bool modified(int comp)
        { if (comp>=0 && comp<TEXTURE_MAX_COMPONENTS) return modified_[comp]; else return false;}

		// Creator of the brick owns the nrrd memory.
		void set_nrrd(const std::shared_ptr<FLIVR::VL_Nrrd> &data, int index)
		{if (index>=0&&index<TEXTURE_MAX_COMPONENTS) data_[index] = data;}
		std::shared_ptr<FLIVR::VL_Nrrd> get_nrrd(int index)
		{if (index>=0&&index<TEXTURE_MAX_COMPONENTS) return data_[index]; else return 0;}
		Nrrd* get_nrrd_raw(int index)
		{if (index >= 0 && index < TEXTURE_MAX_COMPONENTS) return data_[index] ? data_[index]->getNrrd() : nullptr; else return 0;}

		//find out priority
		void set_priority();
		void set_priority_brk(std::ifstream* ifs, int filetype);
		inline int get_priority() {return priority_;}

		virtual VkFormat tex_format(int c);
		virtual void* tex_data(int c);
		virtual void* tex_data_brk(int c, const FileLocInfo* finfo);
		
		bool compute_t_index_min_max(Ray& view, double dt);
		void compute_slicenum(Ray& view, double dt, double& r_tmax, double& r_tmin, unsigned int& r_slicenum);

		void compute_polygons(Ray& view, double tmin, double tmax, double dt,
			vector<double>& vertex, vector<double>& texcoord,
			vector<int>& size);
		void compute_polygons(Ray& view, double dt,
			vector<double>& vertex, vector<double>& texcoord,
			vector<int>& size);
		void compute_polygons2();
		void clear_polygons();
		void compute_polygon(Ray& view, double t,
			vector<double>& vertex, vector<double>& texcoord,
			vector<int>& size);

		void compute_polygons(Ray& view, double dt,
			vector<float>& vertex, vector<uint32_t>& index,
			vector<uint32_t>& size);
		void compute_polygons(Ray& view,
			double tmin, double tmax, double dt,
			vector<float>& vertex, vector<uint32_t>& index,
			vector<uint32_t>& size);


		void get_polygon(int tid, int &size_v, float* &v, int &size_i, uint32_t* &i);
		
		//set d
		void set_d(double d) { d_ = d; }
		//sorting function
		static bool sort_asc(const TextureBrick* b1, const TextureBrick* b2)
		{ return b1->d_ > b2->d_; }
		static bool sort_dsc(const TextureBrick* b1, const TextureBrick* b2)
		{ return b2->d_ > b1->d_; }

		double get_d() {return d_;}

		static bool less_timin(const TextureBrick* b1, const TextureBrick* b2) { return b1->timin_ < b2->timin_; }
		static bool high_timin(const TextureBrick* b1, const TextureBrick* b2) { return b1->timin_ > b2->timin_; }
		static bool less_timax(const TextureBrick* b1, const TextureBrick* b2) { return b1->timax_ < b2->timax_; }
		static bool high_timax(const TextureBrick* b1, const TextureBrick* b2) { return b1->timax_ > b2->timax_; }

		//current index
		inline void set_ind(size_t ind) {ind_ = ind;}
		inline size_t get_ind() {return ind_;}

		void freeBrkData();
		bool isLoaded() {return brkdata_ ? true : false;};
		bool isLoading() {return loading_;}
		void set_loading_state(bool val) {loading_ = val;}
		void set_id_in_loadedbrks(int id) {id_in_loadedbrks = id;};
		int get_id_in_loadedbrks() {return id_in_loadedbrks;}
		int getID() {return findex_;}
		void *getBrickData() {return brkdata_ ? brkdata_->getData() : NULL;}
		std::shared_ptr<VL_Array> getBrickDataSP() {return brkdata_;}
		long getBrickDataSPCount() {return brkdata_.use_count();}

		double dt() {return dt_;}
		double timin() {return timin_;}
		double timax() {return timax_;}
		Ray *vray() {return &vray_;}
		double rate_fac() {return rate_fac_;}
		VolumeRenderer *get_vr() {return vr_;}
		void set_dt(double dt) {dt_ = dt;}
		void set_t_index_min(int timin) {timin_ = timin;}
		void set_t_index_max(int timax) {timax_ = timax;}
		void set_vray(Ray vray) {vray_ = vray;}
		void set_rate_fac(double rate_fac) {rate_fac_ = rate_fac;}
		void set_vr(VolumeRenderer *vr) {vr_ = vr;}

		std::vector<float> *get_vertex_list() {return &vertex_;}
		std::vector<uint32_t> *get_index_list() {return &index_;}
		std::vector<int> *get_v_size_list() {return &size_v_;}

		void set_disp(bool disp) { disp_ = disp; }
		bool get_disp() {return disp_;}

		void set_compression(bool compression) {compression_ = compression;}
		bool get_compression() {return compression_;}

		size_t tex_format_size(VkFormat t);
		VkFormat tex_format_aux(Nrrd* n);

		bool read_brick(char* data, size_t size, const FileLocInfo* finfo);
        
#ifndef _UNIT_TEST_VOLUME_RENDERER_
        static void setCURL(CURL *c) {s_curl_ = c;}
		static void setCURL_Multi(CURLM *c) {s_curlm_ = c;}
		void set_brkdata(const std::shared_ptr<VL_Array> &brkdata) {brkdata_ = brkdata;}
		void set_brkdata(void *brkdata, size_t size) {brkdata_ = std::make_shared<VL_Array>((char *)brkdata, size);}
		static bool read_brick_without_decomp(char* &data, size_t &readsize, FileLocInfo* finfo, wxThread *th=NULL);
		static bool decompress_brick(char *out, char* in, size_t out_size, size_t in_size, int type, int w, int h, int d, int nb, int n5_w = 0, int n5_h = 0, int n5_d = 0);
        static bool raw_decompressor(char *out, char* in, size_t out_size, size_t in_size, bool isn5 = false, int nb = 1, int w = 0, int h = 0, int d = 0, int n5_w = 0, int n5_h = 0, int n5_d = 0);
		static bool jpeg_decompressor(char *out, char* in, size_t out_size, size_t in_size);
		static bool zlib_decompressor(char *out, char* in, size_t out_size, size_t in_size, bool isn5 = false, int nb = 1, int w = 0, int h = 0, int d = 0, int n5_w = 0, int n5_h = 0, int n5_d = 0);
		static bool h265_decompressor(char *out, char* in, size_t out_size, size_t in_size, int w, int h);
		static bool lz4_decompressor(char* out, char* in, size_t out_size, size_t in_size, bool isn5 = false, int nb = 1, int w = 0, int h = 0, int d = 0, int n5_w = 0, int n5_h = 0, int n5_d = 0);
        static bool blosc_decompressor(char* out, char* in, size_t out_size, size_t in_size, bool isn5, int w, int h, int d, int nb, int n5_w = 0, int n5_h = 0, int n5_d = 0);
		static void delete_all_cache_files();
        static char check_machine_endian();

		bool raw_brick_reader(char* data, size_t size, const FileLocInfo* finfo);
		bool jpeg_brick_reader(char* data, size_t size, const FileLocInfo* finfo);
		bool zlib_brick_reader(char* data, size_t size, const FileLocInfo* finfo);
		bool raw_brick_reader_url(char* data, size_t size, const FileLocInfo* finfo);
		bool jpeg_brick_reader_url(char* data, size_t size, const FileLocInfo* finfo);
		bool zlib_brick_reader_url(char* data, size_t size, const FileLocInfo* finfo);

		static size_t WriteMemoryCallback(void* contents, size_t size, size_t nmemb, void* userp);
		static size_t WriteFileCallback(void* contents, size_t size, size_t nmemb, void* userp);
		static int xferinfo(void* p, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow);
		
		static CURL* s_curl_;
		static CURLM* s_curlm_;
#endif

		void prevent_tex_deletion(bool val) {prevent_tex_deletion_ = val;}
		bool is_tex_deletion_prevented() {return prevent_tex_deletion_;}
		void lock_brickdata(bool val) {lock_brickdata_ = val;}
		bool is_brickdata_locked() {return lock_brickdata_;}
	private:
		void compute_edge_rays(BBox &bbox);
		void compute_edge_rays_tex(BBox &bbox);

		//! bbox edges
		Ray edge_[12]; 
		//! tbox edges
		Ray tex_edge_[12]; 
		std::shared_ptr<VL_Nrrd> data_[TEXTURE_MAX_COMPONENTS];
		//! axis sizes (pow2)
		int nx_, ny_, nz_; 
		//! number of components (< TEXTURE_MAX_COMPONENTS)
		int nc_; 
		//type of all the components
		CompType ntype_[TEXTURE_MAX_COMPONENTS];
		//the index of current mask
		int nmask_;
		//the index of current label
		int nlabel_;
		//the index of current stroke
		int nstroke_;
		//! bytes per texel for each component.
		int nb_[TEXTURE_MAX_COMPONENTS]; 
		//! offset into volume texture
		int ox_, oy_, oz_; 
		//! data axis sizes (not necessarily pow2)
		int mx_, my_, mz_; 
		//! bounding box and texcoord box
		BBox bbox_, tbox_, dbox_; 
		Vector view_vector_;
		//a value used for sorting
		//usually distance
		double d_;
		//priority level
		int priority_;//now, 0:highest
		//if it's been drawn in a full update loop
		bool drawn_[TEXTURE_RENDER_MODES];
		//current index in the queue, for reverse searching
		size_t ind_;

		bool dirty_[TEXTURE_MAX_COMPONENTS];
        
        bool skip_[TEXTURE_MAX_COMPONENTS];
        bool modified_[TEXTURE_MAX_COMPONENTS];

		long long offset_;
		long long fsize_;
		std::shared_ptr<VL_Array> brkdata_;
		bool loading_;
		int id_in_loadedbrks;

		bool prevent_tex_deletion_;
		bool lock_brickdata_;

		int findex_;

		double dt_;
		int timax_, timin_;
		Ray vray_;
		double rate_fac_;
		VolumeRenderer *vr_;
		vector<float> vertex_;
		vector<uint32_t> index_;
		vector<int> size_v_;
		vector<int> size_integ_v_;
		vector<int> size_i_;
		vector<int> size_integ_i_;

		bool disp_;

		bool compression_;
        
		static std::map<std::wstring, std::wstring> cache_table_;
		
		static std::map<std::wstring, MemCache*> memcache_table_;
		static std::list<std::wstring> memcache_order;
		static size_t memcache_size;
		static size_t memcache_limit;
	};

	struct Pyramid_Level {
        std::vector<FileLocInfo *> *filenames;
        int filetype;
        std::shared_ptr<FLIVR::VL_Nrrd> data;
        std::vector<TextureBrick *> bricks;
        bool modified[TEXTURE_MAX_COMPONENTS];
	};

} // namespace FLIVR

#endif // Volume_TextureBrick_h
