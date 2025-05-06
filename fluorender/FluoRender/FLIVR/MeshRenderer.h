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

#ifndef SLIVR_MeshRenderer_h
#define SLIVR_MeshRenderer_h

#include "ShaderProgram.h"
#include "MshShader.h"
#include "glm.h"
#include "Plane.h"
#include "BBox.h"
#include "Color.h"
#include "VVulkan.h"
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <glm/glm.hpp>

#include <boost/property_tree/ptree.hpp>
#include <boost/optional.hpp>

#include <tinyxml2.h>

#include "DLLExport.h"

using namespace std;

namespace FLIVR
{
	class MshShaderFactory;

	class EXPORT_API MeshRenderer
	{
	public:
		static constexpr int MSHRENDER_BLEND_DISABLE = 0;
		static constexpr int MSHRENDER_BLEND_OVER = 1;
		static constexpr int MSHRENDER_BLEND_OVER_INV = 2;
		static constexpr int MSHRENDER_BLEND_OVER_UI = 3;
		static constexpr int MSHRENDER_BLEND_ADD = 4;
		static constexpr int MSHRENDER_BLEND_SHADE_SHADOW = 5;
        
        static constexpr int MR_PALETTE_W = 256;
        static constexpr int MR_PALETTE_H = 256;
        static constexpr int MR_PALETTE_SIZE = (MR_PALETTE_W*MR_PALETTE_H);
        static constexpr int MR_PALETTE_ELEM_COMP = 4;

		MeshRenderer(GLMmodel* data);
		MeshRenderer(MeshRenderer&);
		~MeshRenderer();

		void ReleaseVertexBuffers();
        
        void set_model(GLMmodel* data);
        void init_palette();
        void update_palette_tex();
        std::map<vks::VulkanDevice*, std::shared_ptr<vks::VTexture>> get_palette();
        std::map<vks::VulkanDevice*, std::shared_ptr<vks::VTexture>> get_selection_palette();
        void put_node(wstring path, int id=-1);
        void put_node(wstring path, wstring wsid=L"");
        void set_roi_name(wstring name, int id=-1, wstring parent_name=wstring());
        void set_roi_name(wstring name, int id, int parent_id);
        wstring check_new_roi_name(wstring name);
        int add_roi_group_node(int parent_id, wstring name=L"");
        int add_roi_group_node(wstring parent_name=L"", wstring name=L"");
        int get_available_group_id();
        int get_next_sibling_roi(int id);
        void move_roi_node(int src_id, int dst_id, int insert_mode=0);
        bool insert_roi_node(boost::property_tree::wptree& tree, int dst_id, const boost::property_tree::wptree& node, int id, int insert_mode=0);
        void erase_node(int id=-1);
        void erase_node(wstring name);
        wstring get_roi_name(int id=-1);
        void set_roi_select(wstring name, bool select, bool traverse=false);
        void set_roi_select_children(wstring name, bool select, bool traverse=false);
        void set_roi_select_r(const boost::property_tree::wptree& tree, bool select, bool recursive=true);
        void select_all_roi_tree(){ set_roi_select_r(roi_tree_, true); update_palette(desel_palette_mode_, desel_col_fac_); }
        void deselect_all_roi_tree(){ set_roi_select_r(roi_tree_, false); update_palette(desel_palette_mode_, desel_col_fac_); }
        void deselect_all_roi(){ clear_sel_ids_roi_only(); update_palette(desel_palette_mode_, desel_col_fac_); }
        void update_sel_segs();
        void update_sel_segs(const boost::property_tree::wptree& tree);
        boost::property_tree::wptree *get_roi_tree(int id = -INT_MAX);
        boost::optional<wstring> get_roi_path(int id);
        //boost::optional<wstring> get_roi_path(int id, const boost::property_tree::wptree& tree, const wstring& parent);
        boost::optional<wstring> get_roi_path(wstring wsid);
        //boost::optional<wstring> get_roi_path(wstring name, const boost::property_tree::wptree& tree, const wstring& parent);
        int get_roi_id(const wstring &path);
        void set_id_color(unsigned char r, unsigned char g, unsigned char b, bool update=true, const wstring &path=L"");
        void set_id_color(unsigned char r, unsigned char g, unsigned char b, bool update=true, int id=-1);
        void get_id_color(unsigned char &r, unsigned char &g, unsigned char &b, const wstring &path=L"");
        void get_id_color(unsigned char &r, unsigned char &g, unsigned char &b, int id=-1);
        void get_rendered_id_color(unsigned char &r, unsigned char &g, unsigned char &b, int id=-1);
        //0-dark; 1-gray; 2-invisible;
        void update_palette(const wstring &path);
        void update_palette(const boost::property_tree::wptree& tree, bool inherited_state);
        void update_palette(int mode, float fac=-1.0f);
        void select_segment(int id);
        void select_segment(const wstring &path);
        void select_segment(const boost::property_tree::wptree& tree);
        int get_roi_disp_mode(){ return desel_palette_mode_; }
        void set_desel_palette_mode_dark(float fac=0.1);
        void set_desel_palette_mode_gray(float fac=0.1);
        void set_desel_palette_mode_invisible();
        bool is_sel_id(int id);
        void add_sel_id(int id);
        void del_sel_id(int id);
        void set_edit_sel_id(int id);
        int get_edit_sel_id(){ return edit_sel_id_; };
        void clear_sel_ids();
        void clear_sel_ids_roi_only();
        void clear_roi();
        void import_roi_tree_xml(const wstring &filepath);
        void import_roi_tree_xml_r(tinyxml2::XMLElement *lvNode, const boost::property_tree::wptree& tree, const wstring& parent, int& gid);
        wstring export_roi_tree();
        void export_roi_tree_r(wstring &buf, const boost::property_tree::wptree& tree, const wstring& parent);
        string exprot_selected_roi_ids();
        void import_roi_tree(const wstring &tree);
        void import_selected_ids(const string &sel_ids_str);
        bool is_tree() {return !roi_inv_dict_.empty(); }

        struct SubMeshLabel {
            bool state;
            wstring name;
            vector<FLIVR::Point> points;
        };
        void init_group_ids();
        void init_group_ids(const boost::property_tree::wptree& tree, int &count, const wstring& parent);
        void set_roi_state_traverse(int id=-INT_MAX, bool state=true, int exclude=-INT_MAX);
        void set_roi_state_traverse(const boost::property_tree::wptree& tree, bool state=true, int exclude=-INT_MAX);
        void set_roi_state(int id, bool state);
        void set_roi_state_siblings(int id, bool state);
        void set_roi_state_by_name(int id, bool state);
        void toggle_roi_state(int id);
        bool get_roi_state(int id);
        void set_roi_visibility(int id, bool state);
        bool get_roi_visibility(int id);
        map<int, bool> get_all_roi_state();
        bool get_submesh_label_state(int id);
        void set_all_roi_state(const map<int, bool> &states);
        void set_submesh_label(int id, const SubMeshLabel &label_data);
        void set_submesh_label_state(int id, bool state);
        void set_submesh_label_state_siblings(int id, bool state);
        void set_submesh_label_state_by_name(wstring name, bool state);
        map<int, SubMeshLabel>* get_submesh_labels();
        unordered_set<int>* get_active_label_set();
        void set_selected_submesh_id(int id) { selected_submesh_id_ = id; }
        int get_selected_submesh_id() { return selected_submesh_id_; }
        
        boost::property_tree::wptree roi_tree_;
        map<int, wstring> roi_inv_dict_;
        map<int, bool> roi_inv_state_;
        map<int, bool> roi_inv_selection_;
        map<int, SubMeshLabel> roi_labels_;
        unordered_set<int> roi_active_labels_;
        map<wstring, vector<int>> roi_labels_name_dict_;
        bool palette_dirty;
        int selected_submesh_id_;

		//draw
		void draw(const std::unique_ptr<vks::VFrameBuffer>& framebuf, bool clear_framebuf);
		void draw_wireframe(const std::unique_ptr<vks::VFrameBuffer>& framebuf, bool clear_framebuf);
		void draw_integer(unsigned int name, const std::unique_ptr<vks::VFrameBuffer>& framebuf, bool clear_framebuf, VkRect2D scissor = { 0,0,0,0 }, bool segment=false);
		void update();

		//depth peeling
		void set_depth_peel(int peel) {depth_peel_ = peel;}
		int get_depth_peel() {return depth_peel_;}

		//clipping planes
		void set_planes(vector<Plane*> *p);
		vector<Plane*> *get_planes();

		//size limiter
		void set_limit(int val) {limit_ = val;}
		int get_limit() {return limit_;}

		//matrices
		void SetMatrices(glm::mat4 &mv_mat, glm::mat4 &proj_mat)
		{ m_mv_mat = mv_mat; m_proj_mat = proj_mat; }

		//alpha
		void set_alpha(float alpha)
		{ alpha_ = alpha; }
		float get_alpha()
		{ return alpha_; }
		//effects
		void set_lighting(bool val)
		{ light_ = val; }
		bool get_lighting()
		{ return light_; }
		void set_fog(bool use_fog, double fog_intensity, double fog_start, double fog_end, Color fog_col)
		{
			fog_ = use_fog; m_fog_intensity = fog_intensity; m_fog_start = fog_start; m_fog_end = fog_end; m_fog_col = fog_col; 
		}
		bool get_fog()
		{ return fog_; }
        void set_threshold(float th)
        { threshold_ = th; }
        float get_threshold()
        { return threshold_; }

		std::shared_ptr<vks::VTexture> m_depth_tex;

		void set_depth_tex(const std::shared_ptr<vks::VTexture> &depth_tex)
		{
			m_depth_tex = depth_tex;
		}
		std::shared_ptr<vks::VTexture> get_depth_tex()
		{
			return m_depth_tex;
		}

		void set_device(vks::VulkanDevice* device)
		{
			device_ = device;
		}
		vks::VulkanDevice* get_device()
		{
			return device_;
		}
        
        void set_extra_vertex_data(float* data)
        {
            extra_vertex_data_ = data;
        }

		void set_bounds(BBox b) { bounds_ = b; }
		BBox get_bounds() { return bounds_; } 

		struct MeshVertexBuffers {
			vks::Buffer vertexBuffer;
			vks::Buffer indexBuffer;
			uint32_t indexCount;
		};
		
		struct Vertex4 {
			float pos[4];
		};
		struct Vertex44 {
			float pos[4];
			float att[4];
		};
		struct Vertex442 {
			float pos[4];
			float att1[4];
			float att2[2];
		};

		struct MshVertexSettings {
			VkPipelineVertexInputStateCreateInfo inputState;
			std::vector<VkVertexInputBindingDescription> inputBinding;
			std::vector<VkVertexInputAttributeDescription> inputAttributes;
		};

		struct MeshPipeline {
			VkPipeline vkpipeline;
			VkRenderPass renderpass;
			ShaderProgram* shader;
			vks::VulkanDevice* device;
			int blend_mode;
			bool bnormal;
			bool btexcoord;
			VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			VkPolygonMode polymode = VK_POLYGON_MODE_FILL;
		};

		MeshPipeline prepareMeshPipeline(
			vks::VulkanDevice* device,
			int type,
			int blend,
			bool tex,
			VkPrimitiveTopology topo = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
			VkPolygonMode poly = VK_POLYGON_MODE_FILL);

		static VkRenderPass prepareRenderPass(vks::VulkanDevice* device, VkFormat framebuf_format, int attachment_num, bool isSwapChainImage);

		static void init(std::shared_ptr<VVulkan> vulkan);
		static void finalize();

	protected:
		//data and display list
		GLMmodel* data_;
		//depth peeling
		int depth_peel_;	//0:no peeling; 1:peel positive; 2:peel both; -1:peel negative
		//planes
		vector<Plane *> planes_;
		//draw with clipping
		bool draw_clip_;
		int limit_;
		//matrices
		glm::mat4 m_mv_mat, m_proj_mat;
		//lighting
		bool light_;
		//fog
		bool fog_;
		double m_fog_intensity;
		double m_fog_start;
		double m_fog_end;
		Color m_fog_col;
		float alpha_;
        float threshold_;
		//bool update
		bool update_;
		BBox bounds_;
        
        std::map<vks::VulkanDevice*, std::shared_ptr<vks::VTexture>> palette_tex_id_;
        std::map<vks::VulkanDevice*, std::shared_ptr<vks::VTexture>> base_palette_tex_id_;
        std::map<vks::VulkanDevice*, std::shared_ptr<vks::VTexture>> selection_palette_tex_id_;
        unsigned char palette_[MR_PALETTE_SIZE*MR_PALETTE_ELEM_COMP];
        unsigned char base_palette_[MR_PALETTE_SIZE*MR_PALETTE_ELEM_COMP];
        unsigned char selection_palette_[MR_PALETTE_SIZE*MR_PALETTE_ELEM_COMP];
        unordered_set<int> sel_ids_;
        unordered_set<int> sel_segs_;
        int desel_palette_mode_;
        float desel_col_fac_;
        int edit_sel_id_;

		vks::VulkanDevice* device_;
		MshVertexSettings m_vertices4, m_vertices42, m_vertices44, m_vertices442;
		VkPipelineVertexInputStateCreateInfo* m_vin;
		VkFormat depthformat_;
		//vertex buffer
		std::vector<MeshVertexBuffers> m_vertbufs;
		int m_prev_msh_pipeline;

		static std::vector<MeshPipeline> m_msh_pipelines;
		static std::map<vks::VulkanDevice*, VkRenderPass> m_msh_draw_pass;
		static std::map<vks::VulkanDevice*, VkRenderPass> m_msh_intdraw_pass;

		static std::shared_ptr<VVulkan> m_vulkan;

		void setupVertexDescriptions();
        
        float* extra_vertex_data_;
	};

} // End namespace FLIVR

#endif 
