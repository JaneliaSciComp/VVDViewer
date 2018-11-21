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


#include "GL/glew.h"
#include "Ray.h"
#include "BBox.h"
#include "Plane.h"

#include <wx/thread.h>

#include <vector>
#include <fstream>
#include <string>
#include <nrrd.h>
#include <stdint.h>
#include <map>
#include <list>
#include <curl/curl.h>

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
#define BRICK_FILE_TYPE_JPEG	2
#define BRICK_FILE_TYPE_ZLIB	3
#define BRICK_FILE_TYPE_H265	4

	
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
		}
		FileLocInfo(std::wstring filename_, int offset_, int datasize_, int type_, bool isurl_)
		{
			filename = filename_;
			offset = offset_;
			datasize = datasize_;
			type = type_;
			isurl = isurl_;
			cached = false;
			cache_filename = L"";
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
		}

		std::wstring filename;
		long long offset;
		long long datasize;
		int type; //1-raw; 2-jpeg; 3-zlib;
		bool isurl;
		bool cached;
		std::wstring cache_filename;
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
			if (data) delete data;
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
		TextureBrick(Nrrd* n0, Nrrd* n1,
			int nx, int ny, int nz, int nc, int* nb, int ox, int oy, int oz,
			int mx, int my, int mz, const BBox& bbox, const BBox& tbox, const BBox& dbox, int findex = 0, long long offset = 0LL, long long fsize = 0LL);
		virtual ~TextureBrick();

		inline BBox &bbox() { return bbox_; }
		inline BBox &tbox() { return tbox_; }
		inline BBox &dbox() { return dbox_; }

		inline int nx() { return nx_; }
		inline int ny() { return ny_; }
		inline int nz() { return nz_; }
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

		inline int ox() { return ox_; }
		inline int oy() { return oy_; }
		inline int oz() { return oz_; }

		virtual int sx();
		virtual int sy();
		virtual int sz();

		inline void set_drawn(int mode, bool val)
		{ if (mode>=0 && mode<TEXTURE_RENDER_MODES) drawn_[mode] = val; }
		inline void set_drawn(bool val)
		{ for (int i=0; i<TEXTURE_RENDER_MODES; i++) drawn_[i] = val; }
		inline bool drawn(int mode)
		{ if (mode>=0 && mode<TEXTURE_RENDER_MODES) return drawn_[mode]; else return false;}

		// Creator of the brick owns the nrrd memory.
		void set_nrrd(Nrrd* data, int index)
		{if (index>=0&&index<TEXTURE_MAX_COMPONENTS) data_[index] = data;}
		Nrrd* get_nrrd(int index)
		{if (index>=0&&index<TEXTURE_MAX_COMPONENTS) return data_[index]; else return 0;}

		//find out priority
		void set_priority();
		void set_priority_brk(std::ifstream* ifs, int filetype);
		inline int get_priority() {return priority_;}

		virtual GLenum tex_type(int c);
		virtual void* tex_data(int c);
		virtual void* tex_data_brk(int c, const FileLocInfo* finfo);
		
		bool compute_t_index_min_max(Ray& view, double dt);

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
		const void *getBrickData() {return brkdata_;}

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

		void set_disp(bool disp) {disp_ = disp;}
		bool get_disp() {return disp_;}
        
        static void setCURL(CURL *c) {s_curl_ = c;}
		static void setCURL_Multi(CURLM *c) {s_curlm_ = c;}

		size_t tex_type_size(GLenum t);
		GLenum tex_type_aux(Nrrd* n);
		bool read_brick(char* data, size_t size, const FileLocInfo* finfo);
		void set_brkdata(void *brkdata) {brkdata_ = brkdata;}
		static bool read_brick_without_decomp(char* &data, size_t &readsize, FileLocInfo* finfo, wxThread *th=NULL);
		static bool decompress_brick(char *out, char* in, size_t out_size, size_t in_size, int type, int w=0, int h=0);
		static bool jpeg_decompressor(char *out, char* in, size_t out_size, size_t in_size);
		static bool zlib_decompressor(char *out, char* in, size_t out_size, size_t in_size);
		static bool h265_decompressor(char *out, char* in, size_t out_size, size_t in_size, int w, int h);
		static void delete_all_cache_files();

		void prevent_tex_deletion(bool val) {prevent_tex_deletion_ = val;}
		bool is_tex_deletion_prevented() {return prevent_tex_deletion_;}
	private:
		void compute_edge_rays(BBox &bbox);
		void compute_edge_rays_tex(BBox &bbox);
		
		bool raw_brick_reader(char* data, size_t size, const FileLocInfo* finfo);
		bool jpeg_brick_reader(char* data, size_t size, const FileLocInfo* finfo);
		bool zlib_brick_reader(char* data, size_t size, const FileLocInfo* finfo);
		bool raw_brick_reader_url(char* data, size_t size, const FileLocInfo* finfo);
		bool jpeg_brick_reader_url(char* data, size_t size, const FileLocInfo* finfo);
		bool zlib_brick_reader_url(char* data, size_t size, const FileLocInfo* finfo);

		static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp);
		static size_t WriteFileCallback(void *contents, size_t size, size_t nmemb, void *userp);
		static int xferinfo(void *p, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow);

		//! bbox edges
		Ray edge_[12]; 
		//! tbox edges
		Ray tex_edge_[12]; 
		Nrrd* data_[TEXTURE_MAX_COMPONENTS];
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

		long long offset_;
		long long fsize_;
		void *brkdata_;
		bool loading_;
		int id_in_loadedbrks;

		bool prevent_tex_deletion_;

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
        
        static CURL *s_curl_;
		static CURLM *s_curlm_;
		static std::map<std::wstring, std::wstring> cache_table_;
		
		static std::map<std::wstring, MemCache*> memcache_table_;
		static std::list<std::wstring> memcache_order;
		static size_t memcache_size;
		static size_t memcache_limit;
	};

	struct Pyramid_Level {
			std::vector<FileLocInfo *> *filenames;
			int filetype;
			Nrrd* data;
			std::vector<TextureBrick *> bricks;
	};

} // namespace FLIVR

#endif // Volume_TextureBrick_h
