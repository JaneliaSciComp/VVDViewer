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

#include "MeshRenderer.h"
#include <iostream>
#include <vector>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <FLIVR/palettes.h>

#include "compatibility.h"
#include <wx/utils.h>

#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
using boost::property_tree::wptree;

using namespace std;

namespace FLIVR
{
	std::vector<MeshRenderer::MeshPipeline> MeshRenderer::m_msh_pipelines;
	std::map<vks::VulkanDevice*, VkRenderPass> MeshRenderer::m_msh_draw_pass;
	std::map<vks::VulkanDevice*, VkRenderPass> MeshRenderer::m_msh_intdraw_pass;

	std::shared_ptr<VVulkan> MeshRenderer::m_vulkan;

	MeshRenderer::MeshRenderer(GLMmodel* data)
		: data_(data),
		depth_peel_(0),
		draw_clip_(false),
		limit_(-1),
		light_(true),
		fog_(false),
		alpha_(1.0),
        threshold_(0.0f),
		update_(true),
        palette_dirty(false)
	{
		Plane* plane = 0;
		//x
		plane = new Plane(Point(0.0, 0.0, 0.0), Vector(1.0, 0.0, 0.0));
		planes_.push_back(plane);
		plane = new Plane(Point(1.0, 0.0, 0.0), Vector(-1.0, 0.0, 0.0));
		planes_.push_back(plane);
		//y
		plane = new Plane(Point(0.0, 0.0, 0.0), Vector(0.0, 1.0, 0.0));
		planes_.push_back(plane);
		plane = new Plane(Point(0.0, 1.0, 0.0), Vector(0.0, -1.0, 0.0));
		planes_.push_back(plane);
		//z
		plane = new Plane(Point(0.0, 0.0, 0.0), Vector(0.0, 0.0, 1.0));
		planes_.push_back(plane);
		plane = new Plane(Point(0.0, 0.0, 1.0), Vector(0.0, 0.0, -1.0));
		planes_.push_back(plane);
        
        planes_[0]->SetRange(planes_[0]->get_point(), planes_[0]->normal(), planes_[1]->get_point(), planes_[1]->normal());
        planes_[1]->SetRange(planes_[1]->get_point(), planes_[1]->normal(), planes_[0]->get_point(), planes_[0]->normal());
        planes_[2]->SetRange(planes_[2]->get_point(), planes_[2]->normal(), planes_[3]->get_point(), planes_[3]->normal());
        planes_[3]->SetRange(planes_[3]->get_point(), planes_[3]->normal(), planes_[2]->get_point(), planes_[2]->normal());
        planes_[4]->SetRange(planes_[4]->get_point(), planes_[4]->normal(), planes_[5]->get_point(), planes_[5]->normal());
        planes_[5]->SetRange(planes_[5]->get_point(), planes_[5]->normal(), planes_[4]->get_point(), planes_[4]->normal());

		setupVertexDescriptions();

		m_vin = nullptr;

		m_prev_msh_pipeline = -1;
        
        extra_vertex_data_ = nullptr;
        
        init_palette();
	}

	MeshRenderer::MeshRenderer(MeshRenderer &copy)
		: data_(copy.data_),
		depth_peel_(copy.depth_peel_),
		draw_clip_(copy.draw_clip_),
		limit_(copy.limit_),
		light_(copy.light_),
		fog_(copy.fog_),
		alpha_(copy.alpha_),
        threshold_(copy.threshold_),
		update_(true),
		bounds_(copy.bounds_),
		device_(copy.device_),
		depthformat_(copy.depthformat_),
        roi_tree_(copy.roi_tree_),
        palette_dirty(true),
        roi_inv_dict_(copy.roi_inv_dict_),
        roi_inv_state_(copy.roi_inv_state_)
	{
		//clipping planes
		for (int i=0; i<(int)copy.planes_.size(); i++)
		{
			Plane* plane = new Plane(*copy.planes_[i]);
			planes_.push_back(plane);
		}

		m_vertices4 = copy.m_vertices4;
		m_vertices44 = copy.m_vertices44;
		m_vertices42 = copy.m_vertices42;
		m_vertices442 = copy.m_vertices442;

		m_vin = copy.m_vin;

		m_prev_msh_pipeline = copy.m_prev_msh_pipeline;
        
        extra_vertex_data_ = copy.extra_vertex_data_;
        
        
        memcpy(palette_, copy.palette_, sizeof(unsigned char)*MR_PALETTE_SIZE*MR_PALETTE_ELEM_COMP);
        memcpy(base_palette_, copy.base_palette_, sizeof(unsigned char)*MR_PALETTE_SIZE*MR_PALETTE_ELEM_COMP);
        memcpy(selection_palette_, copy.selection_palette_, sizeof(unsigned char)*MR_PALETTE_SIZE*MR_PALETTE_ELEM_COMP);
        
        if (palette_tex_id_.empty())
            m_vulkan->GenTextures2DAllDevice(palette_tex_id_, VK_FORMAT_R8G8B8A8_UNORM, VK_FILTER_NEAREST, MR_PALETTE_W, MR_PALETTE_H);
        m_vulkan->UploadTextures(palette_tex_id_, palette_);
        
        if (selection_palette_tex_id_.empty())
            m_vulkan->GenTextures2DAllDevice(selection_palette_tex_id_, VK_FORMAT_R8G8B8A8_UNORM, VK_FILTER_NEAREST, MR_PALETTE_W, MR_PALETTE_H);
        m_vulkan->UploadTextures(selection_palette_tex_id_, selection_palette_);
        
        if (base_palette_tex_id_.empty())
            m_vulkan->GenTextures2DAllDevice(base_palette_tex_id_, VK_FORMAT_R8G8B8A8_UNORM, VK_FILTER_NEAREST, MR_PALETTE_W, MR_PALETTE_H);
        m_vulkan->UploadTextures(base_palette_tex_id_, base_palette_);
        
        update_palette_tex();
	}

	MeshRenderer::~MeshRenderer()
	{
		//release clipping planes
		for (int i=0; i<(int)planes_.size(); i++)
		{
			if (planes_[i])
				delete planes_[i];
		}
		
		ReleaseVertexBuffers();
	}
	
	void MeshRenderer::ReleaseVertexBuffers()
	{
		for (auto& vb : m_vertbufs)
		{
			device_->destroyBufferInPool(&vb.vertexBuffer);
			device_->destroyBufferInPool(&vb.indexBuffer);
		}
		update_ = true;
	}


bool MeshRenderer::get_submesh_label_state(int id)
{
    auto ite = roi_active_labels_.find(id);
    if (ite != roi_active_labels_.end())
        return true;
    return false;
}

void MeshRenderer::set_submesh_label(int id, const SubMeshLabel &label_data)
{
    if (id < 0)
        return;
    if (roi_inv_dict_.count(id) == 0)
        return;
    
    roi_labels_[id] = label_data;
    
    wstring name = roi_inv_dict_[id];
    auto pos = name.find_last_of(L'.');
    if (pos != wstring::npos && pos+1 < name.length())
        name = name.substr(pos+1);
    
    if (roi_labels_name_dict_.count(name) == 0)
    {
        vector<int> ids;
        ids.push_back(id);
        roi_labels_name_dict_[name] = ids;
    }
    else
        roi_labels_name_dict_[name].push_back(id);
}

void MeshRenderer::set_submesh_label_state(int id, bool state)
{
    if (id < 0)
        return;
    
    if (state)
        roi_active_labels_.insert(id);
    else
    {
        if (roi_active_labels_.empty()) return;

        auto ite = roi_active_labels_.find(id);
        if (ite != roi_active_labels_.end())
            roi_active_labels_.erase(ite);
    }
}

void MeshRenderer::set_submesh_label_state_siblings(int id, bool state)
{
    if (roi_inv_dict_.count(id) == 0)
        return;
    wstring path = roi_inv_dict_[id];
    size_t found = path.find_last_of(L".");
    wstring parent = L"";
    if (found != wstring::npos)
        parent = path.substr(0, found);
    
    boost::property_tree::wptree &tree = roi_tree_.get_child(parent);
    for (wptree::const_iterator child = tree.begin(); child != tree.end(); ++child)
    {
        try
        {
            if ( const auto val = tree.get_optional<wstring>(child->first) )
            {
                int cid = boost::lexical_cast<int>(*val);
                set_submesh_label_state(cid, state);
            }
        }
        catch (boost::bad_lexical_cast e)
        {
            cerr << "MeshRenderer::set_submesh_label_state_siblings(int id, bool state): bad_lexical_cast" << endl;
        }
    }
}
void MeshRenderer::set_submesh_label_state_by_name(wstring name, bool state)
{
    if (roi_labels_name_dict_.count(name) == 0)
        return;
    
    vector<int> ids = roi_labels_name_dict_[name];
    
    for (auto id : ids)
        set_submesh_label_state(id, state);
}
map<int, MeshRenderer::SubMeshLabel>* MeshRenderer::get_submesh_labels()
{
    return &roi_labels_;
}
unordered_set<int>* MeshRenderer::get_active_label_set()
{
    return &roi_active_labels_;
}

void MeshRenderer::set_model(GLMmodel* data)
{
    data_ = data;
    update_ = true;
}

boost::property_tree::wptree *MeshRenderer::get_roi_tree(int id)
{
    if (id == -INT_MAX)
        return &roi_tree_;
    else if(roi_inv_dict_.count(id) > 0)
    {
        wstring path = roi_inv_dict_[id];
        return &roi_tree_.get_child(path);
    }
    
    return NULL;
}

void MeshRenderer::init_palette()
{
    memcpy(palette_, (const void *)palettes::palette_random_256_256_4, sizeof(unsigned char)*MR_PALETTE_SIZE*MR_PALETTE_ELEM_COMP);
    memcpy(base_palette_, (const void *)palettes::palette_random_256_256_4, sizeof(unsigned char)*MR_PALETTE_SIZE*MR_PALETTE_ELEM_COMP);
    
    palette_[0] = 255;
    selection_palette_[0] = 255;
    base_palette_[0] = 255;
    
    if (palette_tex_id_.empty())
        m_vulkan->GenTextures2DAllDevice(palette_tex_id_, VK_FORMAT_R8G8B8A8_UNORM, VK_FILTER_NEAREST, MR_PALETTE_W, MR_PALETTE_H);
    m_vulkan->UploadTextures(palette_tex_id_, palette_);
    
    if (selection_palette_tex_id_.empty())
        m_vulkan->GenTextures2DAllDevice(selection_palette_tex_id_, VK_FORMAT_R8G8B8A8_UNORM, VK_FILTER_NEAREST, MR_PALETTE_W, MR_PALETTE_H);
    m_vulkan->UploadTextures(selection_palette_tex_id_, selection_palette_);
    
    if (base_palette_tex_id_.empty())
        m_vulkan->GenTextures2DAllDevice(base_palette_tex_id_, VK_FORMAT_R8G8B8A8_UNORM, VK_FILTER_NEAREST, MR_PALETTE_W, MR_PALETTE_H);
    m_vulkan->UploadTextures(base_palette_tex_id_, base_palette_);
}

void MeshRenderer::update_palette_tex()
{
    m_vulkan->UploadTextures(palette_tex_id_, palette_);
    m_vulkan->UploadTextures(selection_palette_tex_id_, selection_palette_);
    m_vulkan->UploadTextures(base_palette_tex_id_, base_palette_);
}

std::map<vks::VulkanDevice*, std::shared_ptr<vks::VTexture>> MeshRenderer::get_palette()
{
    if (palette_dirty)
        update_palette_tex();
    
    if (sel_ids_.empty() && roi_tree_.empty()) return base_palette_tex_id_;
    else return palette_tex_id_;
}

std::map<vks::VulkanDevice*, std::shared_ptr<vks::VTexture>> MeshRenderer::get_selection_palette()
{
    if (palette_dirty)
        update_palette_tex();
    
    return selection_palette_tex_id_;
}

void MeshRenderer::update_palette(const wstring &path)
{
    boost::property_tree::wptree &tree = roi_tree_.get_child(path);
    int id = path.empty() ? -INT_MAX : boost::lexical_cast<int>(tree.get_value<wstring>());
    bool state = id == -INT_MAX ? true : roi_inv_state_[id];
    bool pstate = true;
    if (state)
    {
        wstring parent = path;
        size_t found = path.find_last_of(L".");
        while (found != wstring::npos)
        {
            parent = parent.substr(0, found);
            int pid = boost::lexical_cast<int>(roi_tree_.get<wstring>(parent));
            pstate = roi_inv_state_[pid];
            if (!pstate)
                break;
            found = parent.find_last_of(L".");
        }
    }
    if (pstate)
    {
        update_palette(tree, state);
        palette_dirty = true;
    }
}

void MeshRenderer::update_palette(const boost::property_tree::wptree& tree, bool inherited_state)
{
    wstring str_val = tree.get_value<wstring>();
    int id = !str_val.empty() ? boost::lexical_cast<int>(tree.get_value<wstring>()) : -INT_MAX;
    bool state = id != -INT_MAX ? roi_inv_state_[id] && inherited_state : true;
    if (id < 0)
    {
        for (wptree::const_iterator child = tree.begin(); child != tree.end(); ++child)
            update_palette(child->second, state);
    }
    else
    {
        if (state)
        {
            for (int j = 0; j < MR_PALETTE_ELEM_COMP; j++)
                palette_[id * MR_PALETTE_ELEM_COMP + j] = base_palette_[id * MR_PALETTE_ELEM_COMP + j];
        }
        else
        {
            for (int j = 0; j < MR_PALETTE_ELEM_COMP; j++)
                palette_[id * MR_PALETTE_ELEM_COMP + j] = 0;
        }
    }
}


int MeshRenderer::get_roi_id(const wstring &path)
{
    int rval = -INT_MAX;
    
    wstring wsval;
    try
    {
        rval = boost::lexical_cast<int>(roi_tree_.get(path, wsval));
    }
    catch (boost::bad_lexical_cast e)
    {
        cerr << "MeshRenderer::get_roi_id(const wstring &path): bad_lexical_cast" << endl;
        return -INT_MAX;
    }
    
    return rval;
}

void MeshRenderer::select_segment(int id)
{
    wstring path = L"";
    if(roi_inv_dict_.count(id) > 0)
    {
        path = roi_inv_dict_[id];
        selected_submesh_id_ = id;
    }
    else
        selected_submesh_id_ = -INT_MAX;
    
    select_segment(path);
}

void MeshRenderer::select_segment(const wstring &path)
{
    memset(selection_palette_, 0, sizeof(*selection_palette_)*MR_PALETTE_SIZE*MR_PALETTE_ELEM_COMP);
    boost::property_tree::wptree &tree = roi_tree_.get_child(path);
    select_segment(tree);
    palette_dirty = true;
    
    if (!path.empty())
    {
        int id = boost::lexical_cast<int>(tree.get_value<wstring>());
        if (id > 0)
        {
            for (int j = 0; j < MR_PALETTE_ELEM_COMP; j++)
                selection_palette_[id * MR_PALETTE_ELEM_COMP + j] = base_palette_[id * MR_PALETTE_ELEM_COMP + j];
        }
    }
}

void MeshRenderer::select_segment(const boost::property_tree::wptree& tree)
{
    wstring val = tree.get_value<wstring>();
    int id = !val.empty() ? boost::lexical_cast<int>(tree.get_value<wstring>()) : -1;
    if (id < 0)
    {
        for (wptree::const_iterator child = tree.begin(); child != tree.end(); ++child)
            select_segment(child->second);
    }
    else
    {
        for (int j = 0; j < MR_PALETTE_ELEM_COMP; j++)
            selection_palette_[id * MR_PALETTE_ELEM_COMP + j] = base_palette_[id * MR_PALETTE_ELEM_COMP + j];
    }
}

void MeshRenderer::put_node(wstring path, wstring wsid)
{
    int id = boost::lexical_cast<int>(wsid);
    roi_tree_.put(path, wsid);
    roi_inv_dict_[id] = path;
    roi_inv_state_[id] = true;
}
void MeshRenderer::put_node(wstring path, int id)
{
    wstring wsid = boost::lexical_cast<wstring>(id);
    roi_tree_.put(path, wsid);
    roi_inv_dict_[id] = path;
    roi_inv_state_[id] = true;
}

void MeshRenderer::init_group_ids()
{
    int count = 0;
    init_group_ids(roi_tree_, count, L"");
}

void MeshRenderer::init_group_ids(const boost::property_tree::wptree& tree, int &count, const wstring& parent)
{
    for (wptree::const_iterator child = tree.begin(); child != tree.end(); ++child)
    {
        wstring c_path = (parent.empty() ? L"" : parent + L".") + child->first;
        try
        {
            const auto val = tree.get_optional<wstring>(child->first);
            if (!val || val->empty())
            {
                count--;
                put_node(c_path, count);
            }
        }
        catch (boost::bad_lexical_cast e)
        {
            cerr << "MeshRenderer::init_group_ids(const boost::property_tree::wptree& tree, int &count, const wstring& parent): bad_lexical_cast" << endl;
        }
        init_group_ids(child->second, count, parent);
    }
}

void MeshRenderer::set_roi_state(int id, bool state)
{
    roi_inv_state_[id] = state;
    update_palette(roi_inv_dict_[id]);
}
void MeshRenderer::set_roi_state_siblings(int id, bool state)
{
    if (roi_inv_dict_.count(id) == 0)
        return;
    wstring path = roi_inv_dict_[id];
    size_t found = path.find_last_of(L".");
    wstring parent = L"";
    if (found != wstring::npos)
        parent = path.substr(0, found);
    
    boost::property_tree::wptree &tree = roi_tree_.get_child(parent);
    for (wptree::const_iterator child = tree.begin(); child != tree.end(); ++child)
    {
        try
        {
            if ( const auto val = tree.get_optional<wstring>(child->first) )
            {
                int cid = boost::lexical_cast<int>(*val);
                set_roi_visibility(cid, state);
            }
        }
        catch (boost::bad_lexical_cast e)
        {
            cerr << "MeshRenderer::set_roi_state_siblings(int id, bool state): bad_lexical_cast" << endl;
        }
    }
}
void MeshRenderer::set_roi_state_by_name(int id, bool state)
{
    if (roi_inv_dict_.count(id) == 0)
        return;
    
    wstring name = roi_inv_dict_[id];
    auto pos = name.find_last_of(L'.');
    if (pos != wstring::npos && pos+1 < name.length())
        name = name.substr(pos+1);
    
    if (roi_labels_name_dict_.count(name) == 0)
        return;
    
    vector<int> ids = roi_labels_name_dict_[name];
    
    for (auto id : ids)
        set_roi_visibility(id, state);
}
void MeshRenderer::set_roi_state_traverse(int id, bool state, int exclude)
{
    wstring path = id != -INT_MAX ? roi_inv_dict_[id] : L"";
    boost::property_tree::wptree &tree = id != -INT_MAX ? roi_tree_.get_child(path) : roi_tree_;
    bool pstate = true;
    if (state)
    {
        wstring parent = path;
        size_t found = path.find_last_of(L".");
        while (found != wstring::npos)
        {
            parent = parent.substr(0, found);
            int pid = boost::lexical_cast<int>(roi_tree_.get<wstring>(parent));
            roi_inv_state_[pid] = true;
            found = parent.find_last_of(L".");
        }
    }
    
    //if (id != -INT_MAX) roi_inv_state_[id] = state;
    set_roi_state_traverse(tree, state, exclude);
    
    update_palette(path);
}
void MeshRenderer::set_roi_state_traverse(const boost::property_tree::wptree& tree, bool state, int exclude)
{
    try
    {
        wstring val = tree.get_value<wstring>();
        if (!val.empty())
        {
            int id = boost::lexical_cast<int>(val);
            if (id == exclude)
                return;
            roi_inv_state_[id] = state;
        }
    }
    catch (boost::bad_lexical_cast e)
    {
        cerr << "MeshRenderer::set_roi_state_traverse(const wptree& tree, bool state): bad_lexical_cast" << endl;
    }
    
    for (wptree::const_iterator child = tree.begin(); child != tree.end(); ++child)
    {
        set_roi_state_traverse(child->second, state, exclude);
    }
}
void MeshRenderer::toggle_roi_state(int id)
{
    if (roi_inv_state_.count(id) > 0)
    {
        roi_inv_state_[id] = !roi_inv_state_[id];
        update_palette(roi_inv_dict_[id]);
    }
}
bool MeshRenderer::get_roi_state(int id)
{
    if (roi_inv_state_.count(id) > 0)
        return roi_inv_state_[id];
    else
        return false;
}
void MeshRenderer::set_roi_visibility(int id, bool state)
{
    wstring path = id != -INT_MAX ? roi_inv_dict_[id] : L"";
    boost::property_tree::wptree &tree = id != -INT_MAX ? roi_tree_.get_child(path) : roi_tree_;
    bool pstate = true;
    if (state)
    {
        wstring parent = path;
        size_t found = path.find_last_of(L".");
        while (found != wstring::npos)
        {
            parent = parent.substr(0, found);
            int pid = boost::lexical_cast<int>(roi_tree_.get<wstring>(parent));
            roi_inv_state_[pid] = true;
            found = parent.find_last_of(L".");
        }
    }
    set_roi_state(id, state);
}
bool MeshRenderer::get_roi_visibility(int id)
{
    wstring path = id != -INT_MAX ? roi_inv_dict_[id] : L"";
    boost::property_tree::wptree &tree = id != -INT_MAX ? roi_tree_.get_child(path) : roi_tree_;
    bool state = roi_inv_state_[id];
    
    if (state)
    {
        wstring parent = path;
        size_t found = path.find_last_of(L".");
        while (found != wstring::npos)
        {
            parent = parent.substr(0, found);
            int pid = boost::lexical_cast<int>(roi_tree_.get<wstring>(parent));
            if (roi_inv_state_.count(pid) > 0)
            {
                state = state && roi_inv_state_[pid];
                if (!state)
                    break;
            }
            found = parent.find_last_of(L".");
        }
    }
    
    return state;
}
map<int, bool> MeshRenderer::get_all_roi_state()
{
    return roi_inv_state_;
}
void MeshRenderer::set_all_roi_state(const map<int, bool> &states)
{
    roi_inv_state_ = states;
    update_palette(roi_tree_, true);
}

boost::optional<wstring> MeshRenderer::get_roi_path(int id)
{
    return roi_inv_dict_[id];
}
/*
boost::optional<wstring> MeshRenderer::get_roi_path(int id, const wptree& tree, const wstring &parent)
{
    for (wptree::const_iterator child = tree.begin(); child != tree.end(); ++child)
    {
        wstring c_path = (parent.empty() ? L"" : parent + L".") + child->first;
        try
        {
            int val = boost::lexical_cast<int>(child->first);
            if (val == id)
                return c_path;
        }
        catch (boost::bad_lexical_cast e)
        {
            cerr << "MeshRenderer::get_roi_path(int id, const wptree& tree): bad_lexical_cast" << endl;
        }
        if (auto rval = get_roi_path(id, child->second, c_path))
            return rval;
    }

    return boost::none;
}
*/
boost::optional<wstring> MeshRenderer::get_roi_path(wstring wsid)
{
    int id = boost::lexical_cast<int>(wsid);
    return roi_inv_dict_[id];
}
/*
boost::optional<wstring> MeshRenderer::get_roi_path(wstring name, const wptree& tree, const wstring& parent)
{
    for (wptree::const_iterator child = tree.begin(); child != tree.end(); ++child)
    {
        wstring c_path = (parent.empty() ? L"" : parent + L".") + child->first;
        if (const auto val = tree.get_optional<wstring>(child->first))
        {
            if (val == name)
                return c_path;
        }
        if (auto rval = get_roi_path(name, child->second, c_path))
            return rval;
    }

    return boost::none;
}
*/
void MeshRenderer::set_roi_name(wstring name, int id, wstring parent_name)
{
    int edid = (id == -1) ? edit_sel_id_ : id;
    
    if (edid == -1)
        return;

    if (!name.empty())
    {
        if(auto path = get_roi_path(edid))
        {
            wstring newname = check_new_roi_name(name);
            roi_tree_.put(*path, newname);
        }
        else
        {
            wstring prefix(L"");
            if(auto path = get_roi_path(parent_name))
                prefix = *path + L".";
            roi_tree_.add(prefix + boost::lexical_cast<wstring>(edid), name);
        }
    }
    else
    {
        if(auto path = get_roi_path(edid))
        {
            auto pos = path->find_last_of(L'.');
            wstring strid(L""), prefix(L"");
            if (pos != wstring::npos && pos+1 < path->length())
            {
                strid = path->substr(pos+1);
                prefix = path->substr(0, pos);
            }
            else
                strid = *path;
            roi_tree_.get_child(prefix).erase(strid);
        }
    }
}

void MeshRenderer::set_roi_name(wstring name, int id, int parent_id)
{
    int edid = (id == -1) ? edit_sel_id_ : id;
    
    if (edid == -1)
        return;

    if (!name.empty())
    {
        if(auto path = get_roi_path(edid))
        {
            wstring newname = check_new_roi_name(name);
            roi_tree_.put(*path, newname);
        }
        else
        {
            wstring prefix(L"");
            if(auto path = get_roi_path(parent_id))
                prefix = *path + L".";
            roi_tree_.add(prefix + boost::lexical_cast<wstring>(edid), name);
        }
    }
    else
    {
        if(auto path = get_roi_path(edid))
        {
            auto pos = path->find_last_of(L'.');
            wstring strid(L""), prefix(L"");
            if (pos != wstring::npos && pos+1 < path->length())
            {
                strid = path->substr(pos+1);
                prefix = path->substr(0, pos);
            }
            else
                strid = *path;
            roi_tree_.get_child(prefix).erase(strid);
        }
    }
}

wstring MeshRenderer::check_new_roi_name(wstring name)
{
    wstring result = name;
    
    int i;
    for (i=1; get_roi_path(name); i++)
    {
        std::wostringstream w;
        w << "_" << i;
        result = name + w.str();
    }

    return result;
}

int MeshRenderer::get_available_group_id()
{
    int id = -2;
    while (1)
    {
        if (!get_roi_path(id))
            break;
        id--;
    }
    return id;
}

int MeshRenderer::add_roi_group_node(int parent_id, wstring name)
{
    int gid = get_available_group_id();
    wstring prefix(L"");

    if (name.empty())
    {
        wstringstream wss;
        wss << L"Seg Group " << (gid*-1)-1;
        name = wss.str();
    }

    if(auto path = get_roi_path(parent_id))
        prefix = *path + L".";

    roi_tree_.add(prefix + boost::lexical_cast<wstring>(gid), name);

    return gid;
}

int MeshRenderer::add_roi_group_node(wstring parent_name, wstring name)
{
    int pid = parent_name.empty() ? -1 : get_roi_id(parent_name);
    
    return add_roi_group_node(pid, name);
}

//return the parent if there is no sibling.
int MeshRenderer::get_next_sibling_roi(int id)
{
    if(auto path = get_roi_path(id))
    {
        auto pos = path->find_last_of(L'.');
        wstring prefix(L"");
        if (pos != wstring::npos && pos+1 < path->length())
            prefix = path->substr(0, pos);
        
        auto subtree = roi_tree_.get_child_optional(prefix);
        if (subtree)
        {
            bool found_myself = false;
            int prev_id = -1;
            int temp_id = -1;
            for (wptree::const_iterator child = subtree->begin(); child != subtree->end(); ++child)
            {
                wstring strid = child->first;
                int cid = -1;
                try
                {
                    cid = boost::lexical_cast<int>(strid);
                }
                catch (boost::bad_lexical_cast e)
                {
                    cerr << "MeshRenderer::get_next_child_roi(int id): bad_lexical_cast" << endl;
                }

                if (found_myself)
                    return cid;

                if (id == cid)
                {
                    found_myself = true;
                    prev_id = temp_id;
                }
                temp_id = cid;
            }

            if (found_myself)
            {
                if (prev_id != -1)
                    return prev_id;
                else if (!prefix.empty())
                {
                    auto pos = prefix.find_last_of(L'.');
                    wstring parent_strid(L"");
                    if (pos != wstring::npos && pos+1 < prefix.length())
                        parent_strid = prefix.substr(pos+1);
                    else
                        parent_strid = prefix;

                    int pid = -1;
                    try
                    {
                        pid = boost::lexical_cast<int>(parent_strid);
                    }
                    catch (boost::bad_lexical_cast e)
                    {
                        cerr << "MeshRenderer::get_next_child_roi(int id): bad_lexical_cast 2" << endl;
                    }

                    return pid;
                }
            }
        }
    }

    return -1;
}

//insert_mode: 0-before dst; 1-after dst; 2-into group
void MeshRenderer::move_roi_node(int src_id, int dst_id, int insert_mode)
{
    auto path = get_roi_path(src_id);
    if (!path || src_id == dst_id) return;
    
    wptree subtree = roi_tree_.get_child(*path);
    erase_node(src_id);

    if (dst_id != -1)
    {
        if (insert_mode == 0 || insert_mode == 1)
            insert_roi_node(roi_tree_, dst_id, subtree, src_id, insert_mode);
        else if (insert_mode == 2 && dst_id < -1)
        {
            if (auto dst_par_path = get_roi_path(dst_id))
            {
                try
                {
                    wstring dst_path = *dst_par_path + L"." + boost::lexical_cast<wstring>(src_id);
                    roi_tree_.put_child(dst_path, subtree);
                }
                catch (boost::bad_lexical_cast e)
                {
                    cerr << "MeshRenderer::move_roi_node(int src_id, int dst_id): bad_lexical_cast" << endl;
                }
            }
        }
    }
    else
    {
        try
        {
            roi_tree_.put_child(boost::lexical_cast<wstring>(src_id), subtree);
        }
        catch (boost::bad_lexical_cast e)
        {
            cerr << "MeshRenderer::move_roi_node(int src_id, int dst_id): bad_lexical_cast 2" << endl;
        }
    }
}

//insert_mode: 0-before dst; 1-after dst;
bool MeshRenderer::insert_roi_node(wptree& tree, int dst_id, const wptree& node, int id, int insert_mode)
{
    for (wptree::iterator child = tree.begin(); child != tree.end(); ++child)
    {
        wstring strid = child->first;
        int cid = -1;
        try
        {
            cid = boost::lexical_cast<int>(strid);
            if (dst_id == cid)
            {
                wptree::value_type v(boost::lexical_cast<wstring>(id), node);
                if (insert_mode == 1) ++child;
                tree.insert(child, v);
                return true;
            }
        }
        catch (boost::bad_lexical_cast e)
        {
            cerr << "MeshRenderer::insert_roi_node(wptree& tree, int dst_id, const wptree& node, int id): bad_lexical_cast" << endl;
        }
        
        if (insert_roi_node(child->second, dst_id, node, id, insert_mode))
            return true;
    }

    return false;
}

void MeshRenderer::erase_node(int id)
{
    int edid = (id == -1) ? edit_sel_id_ : id;
    
    if(auto path = get_roi_path(edid))
    {
        auto pos = path->find_last_of(L'.');
        wstring strid(L""), parent(L"");
        if (pos != wstring::npos && pos+1 < path->length())
        {
            strid = path->substr(pos+1);
            parent = path->substr(0, pos);
        }
        else
            strid = *path;
        roi_tree_.get_child(parent).erase(strid);
    }
}

void MeshRenderer::erase_node(wstring name)
{
    int edid = get_roi_id(name);
    
    if (edid != -1) erase_node(edid);
}

wstring MeshRenderer::get_roi_name(int id)
{
    wstring rval = roi_inv_dict_[id];
    
    auto pos = rval.find_last_of(L'.');
    if (pos != wstring::npos && pos+1 < rval.length())
        rval = rval.substr(pos+1);

    return rval;
}

void MeshRenderer::set_roi_select(wstring name, bool select, bool traverse)
{
    if (auto path = get_roi_path(name))
    {
        auto pos = path->find_last_of(L'.');
        wstring strid;
        if (pos != wstring::npos && pos+1 < path->length())
            strid = path->substr(pos+1);
        else
            strid = *path;
        try
        {
            int id = boost::lexical_cast<int>(strid);
            if (id != -1)
            {
                if (select)
                    sel_ids_.insert(id);
                else
                {
                    auto ite = sel_ids_.find(id);
                    if (ite != sel_ids_.end())
                        sel_ids_.erase(ite);
                }
            }
        }
        catch (boost::bad_lexical_cast e)
        {
            cerr << "MeshRenderer::set_roi_select(wstring name, bool select): bad_lexical_cast" << endl;
        }

        if (traverse)
        {
            if (auto subtree = roi_tree_.get_child_optional(*path))
                set_roi_select_r(*subtree, select);
        }

        //update_palette(desel_palette_mode_, desel_col_fac_);
    }
}

void MeshRenderer::set_roi_select_children(wstring name, bool select, bool traverse)
{
    if (name.empty())
    {
        set_roi_select_r(roi_tree_, select, traverse);
    }
    else if (auto path = get_roi_path(name))
    {
        if (auto subtree = roi_tree_.get_child_optional(*path))
            set_roi_select_r(*subtree, select, traverse);
    }
    
    //update_palette(desel_palette_mode_, desel_col_fac_);
}

void MeshRenderer::set_roi_select_r(const boost::property_tree::wptree& tree, bool select, bool recursive)
{
    for (wptree::const_iterator child = tree.begin(); child != tree.end(); ++child)
    {
        wstring strid = child->first;
        int id = -1;
        try
        {
            id = boost::lexical_cast<int>(strid);
            if (id != -1)
            {
                if (select)
                    sel_ids_.insert(id);
                else
                {
                    auto ite = sel_ids_.find(id);
                    if (ite != sel_ids_.end())
                        sel_ids_.erase(ite);
                }
            }
        }
        catch (boost::bad_lexical_cast e)
        {
            cerr << "MeshRenderer::toggle_roi_select(boost::property_tree::wptree& tree, bool select): bad_lexical_cast" << endl;
        }

        if (recursive) set_roi_select_r(child->second, select, recursive);
    }
}

void MeshRenderer::update_sel_segs()
{
    sel_segs_.clear();
    update_sel_segs(roi_tree_);

    //add unnamed visible segments
    for(auto ite = sel_ids_.begin(); ite != sel_ids_.end(); ++ite)
    {
        if(!get_roi_path(*ite) && (*ite >= 0))
            sel_segs_.insert(*ite);
    }
}

void MeshRenderer::update_sel_segs(const wptree& tree)
{
    for (wptree::const_iterator child = tree.begin(); child != tree.end(); ++child)
    {
        wstring strid = child->first;
        int id = -1;
        try
        {
            id = boost::lexical_cast<int>(strid);
            if (id != -1)
            {
                auto ite = sel_ids_.find(id);
                if (ite != sel_ids_.end())
                {
                    if (*ite >= 0)
                        sel_segs_.insert(*ite);
                    update_sel_segs(child->second);
                }
            }
        }
        catch (boost::bad_lexical_cast e)
        {
            cerr << "MeshRenderer::update_sel_segs(const wptree& tree): bad_lexical_cast" << endl;
        }
    }
}

void MeshRenderer::set_id_color(unsigned char r, unsigned char g, unsigned char b, bool update, const wstring &path)
{
    int id = path.empty() ? -1 : boost::lexical_cast<int>(roi_tree_.get<wstring>(path));
    set_id_color(r, g, b, update, id);
}

void MeshRenderer::set_id_color(unsigned char r, unsigned char g, unsigned char b, bool update, int id)
{
	int edid = (id == -1) ? edit_sel_id_ : id;

	if (edid < 0 || edid >= MR_PALETTE_SIZE)
		return;

	base_palette_[edid * MR_PALETTE_ELEM_COMP + 0] = r;
	base_palette_[edid * MR_PALETTE_ELEM_COMP + 1] = g;
	base_palette_[edid * MR_PALETTE_ELEM_COMP + 2] = b;
    palette_dirty = true;

	if (update)
		update_palette(L"");
}

void MeshRenderer::get_id_color(unsigned char& r, unsigned char& g, unsigned char& b, const wstring &path)
{
    int id = path.empty() ? -1 : boost::lexical_cast<int>(roi_tree_.get<wstring>(path));
    get_id_color(r, g, b, id);
}

void MeshRenderer::get_id_color(unsigned char& r, unsigned char& g, unsigned char& b, int id)
{
	int edid = (id == -1) ? edit_sel_id_ : id;

	if (edid < 0 || edid >= MR_PALETTE_SIZE)
		return;

	r = base_palette_[edid * MR_PALETTE_ELEM_COMP + 0];
	g = base_palette_[edid * MR_PALETTE_ELEM_COMP + 1];
	b = base_palette_[edid * MR_PALETTE_ELEM_COMP + 2];
}

void MeshRenderer::get_rendered_id_color(unsigned char& r, unsigned char& g, unsigned char& b, int id)
{
	update_palette(desel_palette_mode_, desel_col_fac_);

	int edid = (id == -1) ? edit_sel_id_ : id;

	if (edid < 0 || edid >= MR_PALETTE_SIZE)
		return;

	if (sel_ids_.empty() && roi_tree_.empty())
	{
		r = base_palette_[edid * MR_PALETTE_ELEM_COMP + 0];
		g = base_palette_[edid * MR_PALETTE_ELEM_COMP + 1];
		b = base_palette_[edid * MR_PALETTE_ELEM_COMP + 2];
	}
	else
	{
		r = palette_[edid * MR_PALETTE_ELEM_COMP + 0];
		g = palette_[edid * MR_PALETTE_ELEM_COMP + 1];
		b = palette_[edid * MR_PALETTE_ELEM_COMP + 2];
	}
}

void MeshRenderer::set_desel_palette_mode_dark(float fac)
{
	for (int i = 1; i < MR_PALETTE_SIZE; i++)
	{
		for (int j = 0; j < 3; j++)
			palette_[i * MR_PALETTE_ELEM_COMP + j] = (unsigned char)(base_palette_[i * MR_PALETTE_ELEM_COMP + j] * fac);
		palette_[i * MR_PALETTE_ELEM_COMP + 3] = (unsigned char)255;
	}
	palette_[0] = 0; palette_[1] = 0; palette_[2] = 0; palette_[3] = 0;

	for (auto ite = sel_segs_.begin(); ite != sel_segs_.end(); ++ite)
		for (int j = 0; j < MR_PALETTE_ELEM_COMP; j++)
			if ((*ite) >= 0) palette_[(*ite) * MR_PALETTE_ELEM_COMP + j] = base_palette_[(*ite) * MR_PALETTE_ELEM_COMP + j];
	/*
			for (int i = 0; i < 256*64; i++)
				for (int j = 0; j < MR_PALETTE_ELEM_COMP; j++)
					palette_[i*MR_PALETTE_ELEM_COMP+j] = base_palette_[i*MR_PALETTE_ELEM_COMP+j];
	*/
	update_palette_tex();
}

void MeshRenderer::set_desel_palette_mode_gray(float fac)
{
	for (int i = 1; i < MR_PALETTE_SIZE; i++)
	{
		for (int j = 0; j < 3; j++)
			palette_[i * MR_PALETTE_ELEM_COMP + j] = (unsigned char)(128.0 * fac);
		palette_[i * MR_PALETTE_ELEM_COMP + 3] = (unsigned char)255;
	}
	palette_[0] = 0; palette_[1] = 0; palette_[2] = 0; palette_[3] = 0;

	for (auto ite = sel_segs_.begin(); ite != sel_segs_.end(); ++ite)
		for (int j = 0; j < MR_PALETTE_ELEM_COMP; j++)
			if ((*ite) >= 0) palette_[(*ite) * MR_PALETTE_ELEM_COMP + j] = base_palette_[(*ite) * MR_PALETTE_ELEM_COMP + j];

	update_palette_tex();
}

void MeshRenderer::set_desel_palette_mode_invisible()
{
	for (int i = 0; i < MR_PALETTE_SIZE; i++)
		for (int j = 0; j < 4; j++)
			palette_[i * MR_PALETTE_ELEM_COMP + j] = 0;

	for (auto ite = sel_segs_.begin(); ite != sel_segs_.end(); ++ite)
		for (int j = 0; j < MR_PALETTE_ELEM_COMP; j++)
			if ((*ite) >= 0) palette_[(*ite) * MR_PALETTE_ELEM_COMP + j] = base_palette_[(*ite) * MR_PALETTE_ELEM_COMP + j];

	update_palette_tex();
}

void MeshRenderer::update_palette(int mode, float fac)
{
	update_sel_segs();

	switch (mode)
	{
	case 0:
		if (fac < 0.0f) fac = 0.3f;
		set_desel_palette_mode_dark(fac);
		break;
	case 1:
		if (fac < 0.0f) fac = 0.1f;
		set_desel_palette_mode_gray(fac);
		break;
	case 2:
		set_desel_palette_mode_invisible();
		break;
	}

	desel_palette_mode_ = mode;
	desel_col_fac_ = fac;
}

bool MeshRenderer::is_sel_id(int id)
{
	/*
    auto ite = sel_ids_.find(id);

	if (ite != sel_ids_.end())
		return true;
	else
		return false;
     */
    return true;
}

void MeshRenderer::add_sel_id(int id)
{
	if (id == -1)
		return;

	sel_ids_.insert(id);
	edit_sel_id_ = id;

	update_palette(desel_palette_mode_, desel_col_fac_);
}

void MeshRenderer::del_sel_id(int id)
{
	if (sel_ids_.empty()) return;

	auto ite = sel_ids_.find(id);
	if (ite != sel_ids_.end())
	{
		sel_ids_.erase(ite);
		if (edit_sel_id_ == id) edit_sel_id_ = -1;
	}

	update_palette(desel_palette_mode_, desel_col_fac_);
}

void MeshRenderer::set_edit_sel_id(int id)
{
	edit_sel_id_ = id;
}

void MeshRenderer::clear_sel_ids()
{
	if (!sel_ids_.empty()) sel_ids_.clear();

	edit_sel_id_ = -1;

	update_palette(desel_palette_mode_, desel_col_fac_);
}

void MeshRenderer::clear_sel_ids_roi_only()
{
	auto ite = sel_ids_.begin();
	while (ite != sel_ids_.end())
	{
		if ((*ite) >= 0)
			ite = sel_ids_.erase(ite);
		else
			++ite;
	}

	if (edit_sel_id_ >= 0) edit_sel_id_ = -1;

	update_palette(desel_palette_mode_, desel_col_fac_);
}

void MeshRenderer::clear_roi()
{
	if (!roi_tree_.empty()) roi_tree_.clear();

	clear_sel_ids();
}

wstring MeshRenderer::export_roi_tree()
{
	wstring str;

	export_roi_tree_r(str, roi_tree_, wstring(L""));

	return str;
}

void MeshRenderer::export_roi_tree_r(wstring& buf, const wptree& tree, const wstring& parent)
{
	for (wptree::const_iterator child = tree.begin(); child != tree.end(); ++child)
	{
		wstring c_path = (parent.empty() ? L"" : parent + L".") + child->first;
		wstring strid = child->first;
		int id = -1;
		try
		{
			id = boost::lexical_cast<int>(strid);
			if (id != -1)
			{
				if (const auto val = tree.get_optional<wstring>(child->first))
				{
					buf += c_path + L"\n";	//path
					buf += *val + L"\n";	//name

					//id and color
					wstringstream wss;
					unsigned char r = 0, g = 0, b = 0;
					get_id_color(r, g, b, id);
					wss << id << L" " << (int)r << L" " << (int)g << L" " << (int)b << L"\n";
					buf += wss.str();
				}
			}
		}
		catch (boost::bad_lexical_cast e)
		{
			cerr << "TextureRenderer::update_sel_segs(const wptree& tree): bad_lexical_cast" << endl;
		}
		export_roi_tree_r(buf, child->second, c_path);
	}
}

string MeshRenderer::exprot_selected_roi_ids()
{
	stringstream ss;
	for (auto ite = sel_ids_.begin(); ite != sel_ids_.end(); ++ite)
		ss << *ite << " ";

	return ss.str();
}

void MeshRenderer::import_roi_tree_xml(const wstring& filepath)
{
	tinyxml2::XMLDocument doc;
	if (doc.LoadFile(ws2s(filepath).c_str()) != 0) {
		return;
	}

	tinyxml2::XMLElement* root = doc.RootElement();
	if (!root || strcmp(root->Name(), "Metadata"))
		return;

	init_palette();
	roi_tree_.clear();

	tinyxml2::XMLElement* child = root->FirstChildElement();
	while (child)
	{
		if (child->Name())
		{
			if (strcmp(child->Name(), "ROI_Tree") == 0)
			{
				int gid = -2;
				import_roi_tree_xml_r(child, roi_tree_, wstring(L""), gid);
			}
		}
		child = child->NextSiblingElement();
	}

	update_palette(desel_palette_mode_, desel_col_fac_);
}

void MeshRenderer::import_roi_tree_xml_r(tinyxml2::XMLElement* lvNode, const wptree& tree, const wstring& parent, int& gid)
{
	tinyxml2::XMLElement* child = lvNode->FirstChildElement();
	while (child)
	{
		if (child->Name() && child->Attribute("name"))
		{
			try
			{
				wstring name = s2ws(child->Attribute("name"));
				if (strcmp(child->Name(), "Group") == 0)
				{
					wstringstream wss;
					wss << (parent.empty() ? L"" : parent + L".") << gid;
					wstring c_path = wss.str();

					roi_tree_.add(c_path, name);
					import_roi_tree_xml_r(child, tree, c_path, --gid);
				}
				if (strcmp(child->Name(), "ROI") == 0 && child->Attribute("id"))
				{
					string strid = child->Attribute("id");
					int id = boost::lexical_cast<int>(strid);
					if (id >= 0 && id < MR_PALETTE_SIZE && child->Attribute("r") && child->Attribute("g") && child->Attribute("b"))
					{
						wstring c_path = (parent.empty() ? L"" : parent + L".") + s2ws(strid);
						string strR = child->Attribute("r");
						string strG = child->Attribute("g");
						string strB = child->Attribute("b");
						int r = boost::lexical_cast<int>(strR);
						int g = boost::lexical_cast<int>(strG);
						int b = boost::lexical_cast<int>(strB);
						roi_tree_.add(c_path, name);
						set_id_color(r, g, b, false, id);
					}
				}
			}
			catch (boost::bad_lexical_cast e)
			{
				cerr << "TextureRenderer::import_roi_tree_xml_r(XMLElement *lvNode, const wptree& tree, const wstring& parent): bad_lexical_cast" << endl;
			}
		}
		child = child->NextSiblingElement();
	}
}

void MeshRenderer::import_roi_tree(const wstring& tree)
{
	wistringstream wiss(tree);

	init_palette();

	while (1)
	{
		wstring path, name, strcol;

		if (!getline(wiss, path))
			break;
		if (!getline(wiss, name))
			break;
		if (!getline(wiss, strcol))
			break;

		vector<int> col;
		int ival;
		std::wistringstream ls(strcol);
		while (ls >> ival)
			col.push_back(ival);

		if (col.size() < 4)
			break;

		roi_tree_.add(path, name);
		set_id_color((unsigned char)col[1], (unsigned char)col[2], (unsigned char)col[3], false, col[0]);
	}

	update_palette(desel_palette_mode_, desel_col_fac_);
}

void MeshRenderer::import_selected_ids(const string& sel_ids_str)
{
	istringstream iss(sel_ids_str);

	clear_sel_ids();

	int id;
	while (iss >> id)
	{
		if (id != 0 && id != -1)
			add_sel_id(id);
	}

	update_palette(desel_palette_mode_, desel_col_fac_);
}

	void MeshRenderer::init(std::shared_ptr<VVulkan> vulkan)
	{
		m_vulkan = vulkan;

		for (auto dev : m_vulkan->devices)
		{
			if (m_msh_draw_pass.count(dev) == 0)
				m_msh_draw_pass[dev] = prepareRenderPass(dev, VK_FORMAT_R32G32B32A32_SFLOAT, 1, false);
			if (m_msh_intdraw_pass.count(dev) == 0)
				m_msh_intdraw_pass[dev] = prepareRenderPass(dev, VK_FORMAT_R32_UINT, 1, false);
		}
	}

	void MeshRenderer::finalize()
	{
		if (m_vulkan)
		{
			for (auto dev : m_vulkan->devices)
			{
				vkDestroyRenderPass(dev->logicalDevice, m_msh_draw_pass[dev], nullptr);
				vkDestroyRenderPass(dev->logicalDevice, m_msh_intdraw_pass[dev], nullptr);
			}

			for (auto& p : m_msh_pipelines)
			{
				vkDestroyPipeline(p.device->logicalDevice, p.vkpipeline, nullptr);
			}
		}
	}

	void MeshRenderer::setupVertexDescriptions()
	{
		// Binding description
		m_vertices4.inputBinding.resize(1);
		m_vertices4.inputBinding[0] =
			vks::initializers::vertexInputBindingDescription(
				0,
				sizeof(Vertex4),
				VK_VERTEX_INPUT_RATE_VERTEX);
		// Attribute descriptions
		// Describes memory layout and shader positions
		m_vertices4.inputAttributes.resize(1);
		// Location 0 : Position
		m_vertices4.inputAttributes[0] =
			vks::initializers::vertexInputAttributeDescription(
				0,
				0,
				VK_FORMAT_R32G32B32A32_SFLOAT,
				offsetof(Vertex4, pos));

		m_vertices4.inputState = vks::initializers::pipelineVertexInputStateCreateInfo();
		m_vertices4.inputState.vertexBindingDescriptionCount = static_cast<uint32_t>(m_vertices4.inputBinding.size());
		m_vertices4.inputState.pVertexBindingDescriptions = m_vertices4.inputBinding.data();
		m_vertices4.inputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(m_vertices4.inputAttributes.size());
		m_vertices4.inputState.pVertexAttributeDescriptions = m_vertices4.inputAttributes.data();

		// Binding description
		m_vertices42.inputBinding.resize(1);
		m_vertices42.inputBinding[0] =
			vks::initializers::vertexInputBindingDescription(
				0,
				sizeof(float)*6,
				VK_VERTEX_INPUT_RATE_VERTEX);
		// Attribute descriptions
		// Describes memory layout and shader positions
		m_vertices42.inputAttributes.resize(2);
		// Location 0 : Position
		m_vertices42.inputAttributes[0] =
			vks::initializers::vertexInputAttributeDescription(
				0,
				0,
				VK_FORMAT_R32G32B32A32_SFLOAT,
				0);
		// Location 1 : Texture
		m_vertices42.inputAttributes[1] =
			vks::initializers::vertexInputAttributeDescription(
				0,
				1,
				VK_FORMAT_R32G32_SFLOAT,
				sizeof(float)*4);

		m_vertices42.inputState = vks::initializers::pipelineVertexInputStateCreateInfo();
		m_vertices42.inputState.vertexBindingDescriptionCount = static_cast<uint32_t>(m_vertices42.inputBinding.size());
		m_vertices42.inputState.pVertexBindingDescriptions = m_vertices42.inputBinding.data();
		m_vertices42.inputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(m_vertices42.inputAttributes.size());
		m_vertices42.inputState.pVertexAttributeDescriptions = m_vertices42.inputAttributes.data();

		// Binding description
		m_vertices44.inputBinding.resize(1);
		m_vertices44.inputBinding[0] =
			vks::initializers::vertexInputBindingDescription(
				0,
				sizeof(Vertex44),
				VK_VERTEX_INPUT_RATE_VERTEX);
		// Attribute descriptions
		// Describes memory layout and shader positions
		m_vertices44.inputAttributes.resize(2);
		// Location 0 : Position
		m_vertices44.inputAttributes[0] =
			vks::initializers::vertexInputAttributeDescription(
				0,
				0,
				VK_FORMAT_R32G32B32A32_SFLOAT,
				offsetof(Vertex44, pos));
		// Location 1 : Normal
		m_vertices44.inputAttributes[1] =
			vks::initializers::vertexInputAttributeDescription(
				0,
				1,
				VK_FORMAT_R32G32B32A32_SFLOAT,
				offsetof(Vertex44, att));

		m_vertices44.inputState = vks::initializers::pipelineVertexInputStateCreateInfo();
		m_vertices44.inputState.vertexBindingDescriptionCount = static_cast<uint32_t>(m_vertices44.inputBinding.size());
		m_vertices44.inputState.pVertexBindingDescriptions = m_vertices44.inputBinding.data();
		m_vertices44.inputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(m_vertices44.inputAttributes.size());
		m_vertices44.inputState.pVertexAttributeDescriptions = m_vertices44.inputAttributes.data();

		// Binding description
		m_vertices442.inputBinding.resize(1);
		m_vertices442.inputBinding[0] =
			vks::initializers::vertexInputBindingDescription(
				0,
				sizeof(Vertex442),
				VK_VERTEX_INPUT_RATE_VERTEX);
		// Attribute descriptions
		// Describes memory layout and shader positions
		m_vertices442.inputAttributes.resize(3);
		// Location 0 : Position
		m_vertices442.inputAttributes[0] =
			vks::initializers::vertexInputAttributeDescription(
				0,
				0,
				VK_FORMAT_R32G32B32A32_SFLOAT,
				offsetof(Vertex442, pos));
		m_vertices442.inputAttributes[1] =
			vks::initializers::vertexInputAttributeDescription(
				0,
				1,
				VK_FORMAT_R32G32B32A32_SFLOAT,
				offsetof(Vertex442, att1));
		m_vertices442.inputAttributes[2] =
			vks::initializers::vertexInputAttributeDescription(
				0,
				2,
				VK_FORMAT_R32G32_SFLOAT,
				offsetof(Vertex442, att2));

		m_vertices442.inputState = vks::initializers::pipelineVertexInputStateCreateInfo();
		m_vertices442.inputState.vertexBindingDescriptionCount = static_cast<uint32_t>(m_vertices442.inputBinding.size());
		m_vertices442.inputState.pVertexBindingDescriptions = m_vertices442.inputBinding.data();
		m_vertices442.inputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(m_vertices442.inputAttributes.size());
		m_vertices442.inputState.pVertexAttributeDescriptions = m_vertices442.inputAttributes.data();
	}

	VkRenderPass MeshRenderer::prepareRenderPass(vks::VulkanDevice* device, VkFormat framebuf_format, int attachment_num, bool isSwapChainImage)
	{
		VkRenderPass pass = VK_NULL_HANDLE;

		VkPhysicalDevice physicalDevice = device->physicalDevice;
		
		// Create a separate render pass for the offscreen rendering as it may differ from the one used for scene rendering
		std::vector<VkAttachmentDescription> attchmentDescriptions = {};
		std::vector<VkAttachmentReference> colorReferences;
		for (int i = 0; i < attachment_num; i++)
		{
			// Color attachment
			VkAttachmentDescription attd = {};
			attd.format = framebuf_format;
			attd.samples = VK_SAMPLE_COUNT_1_BIT;
			attd.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
			attd.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			attd.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attd.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attd.initialLayout = isSwapChainImage ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			attd.finalLayout = isSwapChainImage ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			attchmentDescriptions.push_back(attd);

			VkAttachmentReference colref = { (uint32_t)i, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
			colorReferences.push_back(colref);
		}
		VkAttachmentDescription attd_d = {};
		attd_d.format = m_vulkan->depthFormat;
		attd_d.samples = VK_SAMPLE_COUNT_1_BIT;
		attd_d.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		attd_d.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attd_d.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attd_d.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attd_d.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		attd_d.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		attchmentDescriptions.push_back(attd_d);
		
		VkAttachmentReference depthReference = {};
		depthReference.attachment = 1;
		depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;


		VkSubpassDescription subpassDescription = {};
		subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpassDescription.colorAttachmentCount = static_cast<uint32_t>(colorReferences.size());
		subpassDescription.pColorAttachments = colorReferences.data();
		subpassDescription.pDepthStencilAttachment = &depthReference;

		// Use subpass dependencies for layout transitions
		std::array<VkSubpassDependency, 2> dependencies;

		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		// Create the actual renderpass
		VkRenderPassCreateInfo renderPassInfo = {};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = static_cast<uint32_t>(attchmentDescriptions.size());
		renderPassInfo.pAttachments = attchmentDescriptions.data();
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpassDescription;
		renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
		renderPassInfo.pDependencies = dependencies.data();

		VK_CHECK_RESULT(vkCreateRenderPass(device->logicalDevice, &renderPassInfo, nullptr, &pass));

		return pass;
	}

	MeshRenderer::MeshPipeline MeshRenderer::prepareMeshPipeline(vks::VulkanDevice* device, int type, int blend, bool tex, VkPrimitiveTopology topo, VkPolygonMode poly)
	{
		MeshPipeline ret_pipeline;

		ShaderProgram* shader = m_vulkan->msh_shader_factory_->shader(
			device->logicalDevice,
			type, depth_peel_, tex && data_->texcoords, fog_, light_ && (data_->normals || data_->facetnorms), extra_vertex_data_ != nullptr);

		if (m_prev_msh_pipeline >= 0) {
			if (m_msh_pipelines[m_prev_msh_pipeline].device == device &&
				m_msh_pipelines[m_prev_msh_pipeline].shader == shader &&
				m_msh_pipelines[m_prev_msh_pipeline].blend_mode == blend &&
				m_msh_pipelines[m_prev_msh_pipeline].topology == topo &&
				m_msh_pipelines[m_prev_msh_pipeline].polymode == poly)
				return m_msh_pipelines[m_prev_msh_pipeline];
		}
		for (int i = 0; i < m_msh_pipelines.size(); i++) {
			if (m_msh_pipelines[i].device == device &&
				m_msh_pipelines[i].shader == shader &&
				m_msh_pipelines[i].blend_mode == blend &&
				m_msh_pipelines[i].topology == topo &&
				m_msh_pipelines[i].polymode == poly)
			{
				m_prev_msh_pipeline = i;
				return m_msh_pipelines[i];
			}
		}

		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
			vks::initializers::pipelineInputAssemblyStateCreateInfo(
				topo,
				0,
				VK_FALSE);

		VkPipelineRasterizationStateCreateInfo rasterizationState =
			vks::initializers::pipelineRasterizationStateCreateInfo(
				poly,
				VK_CULL_MODE_NONE,
				VK_FRONT_FACE_COUNTER_CLOCKWISE,
				0);

		VkPipelineDepthStencilStateCreateInfo depthStencilState =
			vks::initializers::pipelineDepthStencilStateCreateInfo(
				VK_TRUE,
				VK_TRUE,
				VK_COMPARE_OP_LESS_OR_EQUAL);

		VkPipelineViewportStateCreateInfo viewportState =
			vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);

		VkPipelineMultisampleStateCreateInfo multisampleState =
			vks::initializers::pipelineMultisampleStateCreateInfo(
				VK_SAMPLE_COUNT_1_BIT,
				0);

		std::vector<VkDynamicState> dynamicStateEnables = {
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR
		};
		VkPipelineDynamicStateCreateInfo dynamicState =
			vks::initializers::pipelineDynamicStateCreateInfo(
				dynamicStateEnables.data(),
				static_cast<uint32_t>(dynamicStateEnables.size()),
				0);

		VkRenderPass renderpass = (type == 1 || type == 2) ? m_msh_intdraw_pass[device] : m_msh_draw_pass[device];

		MshShaderFactory::MshPipelineSettings pipeline_settings = m_vulkan->msh_shader_factory_->pipeline_[device];
		VkGraphicsPipelineCreateInfo pipelineCreateInfo =
			vks::initializers::pipelineCreateInfo(
				pipeline_settings.pipelineLayout,
				renderpass,
				0);

		pipelineCreateInfo.pVertexInputState = m_vin;
		pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
		pipelineCreateInfo.pRasterizationState = &rasterizationState;
		pipelineCreateInfo.pMultisampleState = &multisampleState;
		pipelineCreateInfo.pViewportState = &viewportState;
		pipelineCreateInfo.pDepthStencilState = &depthStencilState;
		pipelineCreateInfo.pDynamicState = &dynamicState;
		pipelineCreateInfo.stageCount = 2;

		//blend mode
		VkBool32 enable_blend;
		VkBlendOp blend_op;
		VkBlendFactor src_blend, dst_blend;

		enable_blend = VK_TRUE;
		switch (blend)
		{
		case MSHRENDER_BLEND_OVER:
			enable_blend = VK_TRUE;
			blend_op = VK_BLEND_OP_ADD;
			src_blend = VK_BLEND_FACTOR_ONE;
			dst_blend = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			break;
		case MSHRENDER_BLEND_OVER_INV:
			enable_blend = VK_TRUE;
			blend_op = VK_BLEND_OP_ADD;
			src_blend = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
			dst_blend = VK_BLEND_FACTOR_ONE;
			break;
		case MSHRENDER_BLEND_OVER_UI:
			enable_blend = VK_TRUE;
			blend_op = VK_BLEND_OP_ADD;
			src_blend = VK_BLEND_FACTOR_SRC_ALPHA;
			dst_blend = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			break;
		case MSHRENDER_BLEND_ADD:
			enable_blend = VK_TRUE;
			blend_op = VK_BLEND_OP_ADD;
			src_blend = VK_BLEND_FACTOR_ONE;
			dst_blend = VK_BLEND_FACTOR_ONE;
			break;
		case MSHRENDER_BLEND_SHADE_SHADOW:
			enable_blend = VK_TRUE;
			blend_op = VK_BLEND_OP_MIN;
			src_blend = VK_BLEND_FACTOR_ZERO;
			dst_blend = VK_BLEND_FACTOR_SRC_COLOR;
			break;
		default:
			enable_blend = VK_FALSE;
			blend_op = VK_BLEND_OP_ADD;
			src_blend = VK_BLEND_FACTOR_ONE;
			dst_blend = VK_BLEND_FACTOR_ZERO;
		}

		VkPipelineColorBlendAttachmentState blendAttachmentState =
			vks::initializers::pipelineColorBlendAttachmentState(
				enable_blend,
				blend_op,
				src_blend,
				dst_blend,
				0xf);

		VkPipelineColorBlendStateCreateInfo colorBlendState =
			vks::initializers::pipelineColorBlendStateCreateInfo(
				1,
				&blendAttachmentState);
		pipelineCreateInfo.pColorBlendState = &colorBlendState;

		// Load shaders
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;
		shaderStages[0] = shader->get_vertex_shader();
		shaderStages[1] = shader->get_fragment_shader();
		pipelineCreateInfo.pStages = shaderStages.data();

		VK_CHECK_RESULT(
			vkCreateGraphicsPipelines(
				device->logicalDevice,
				m_vulkan->getPipelineCache(),
				1,
				&pipelineCreateInfo,
				nullptr,
				&ret_pipeline.vkpipeline
			)
		);

		ret_pipeline.device = device;
		ret_pipeline.renderpass = renderpass;
		ret_pipeline.shader = shader;
		ret_pipeline.blend_mode = blend;
		ret_pipeline.topology = topo;
		ret_pipeline.polymode = poly;

		m_msh_pipelines.push_back(ret_pipeline);
		m_prev_msh_pipeline = m_msh_pipelines.size() - 1;

		return ret_pipeline;
	}

	//clipping planes
	void MeshRenderer::set_planes(vector<Plane*> *p)
	{
		int i;
		if (!planes_.empty())
		{
			for (i=0; i<(int)planes_.size(); i++)
			{
				if (planes_[i])
					delete planes_[i];
			}
			planes_.clear();
		}

		for (i=0; i<(int)p->size(); i++)
		{
			Plane *plane = new Plane(*(*p)[i]);
			planes_.push_back(plane);
		}
	}

	vector<Plane*>* MeshRenderer::get_planes()
	{
		return &planes_;
	}

	void MeshRenderer::update()
	{
		for (auto& vb : m_vertbufs)
		{
			vb.vertexBuffer.destroy();
			vb.indexBuffer.destroy();
			vb.indexCount = 0;
		}
		m_vertbufs.clear();
        
		bool bnormal = data_->normals || data_->facetnorms;
		bool btexcoord = data_->texcoords;

		if (bnormal && btexcoord)
			m_vin = &m_vertices442.inputState;
		else if (bnormal)
			m_vin = &m_vertices44.inputState;
		else if (btexcoord)
			m_vin = &m_vertices42.inputState;
		else
			m_vin = &m_vertices4.inputState;

		vector<float> verts;
		vector<uint32_t> ids;

		GLMgroup* group = data_->groups;
		GLMtriangle* triangle = 0;
		uint32_t count = 0;
		while (group)
		{
			//verts.clear();
			//ids.clear();
			for (size_t i=0; i<group->numtriangles; ++i)
			{
				triangle = &(data_->triangles[group->triangles[i]]);
				for (size_t j=0; j<3; ++j)
				{
					verts.push_back(data_->vertices[3*triangle->vindices[j]]);
					verts.push_back(data_->vertices[3*triangle->vindices[j]+1]);
					verts.push_back(data_->vertices[3*triangle->vindices[j]+2]);
                    if (!extra_vertex_data_)
                        verts.push_back(1.0f);
                    else
                        verts.push_back(extra_vertex_data_[triangle->vindices[j]]);
					if (bnormal)
					{
                        if (data_->normals)
                        {
                            verts.push_back(data_->normals[3*triangle->nindices[j]]);
                            verts.push_back(data_->normals[3*triangle->nindices[j]+1]);
                            verts.push_back(data_->normals[3*triangle->nindices[j]+2]);
                            verts.push_back(0.0f);
                        }
                        else if (data_->facetnorms)
                        {
                            verts.push_back(data_->facetnorms[3*triangle->findex]);
                            verts.push_back(data_->facetnorms[3*triangle->findex+1]);
                            verts.push_back(data_->facetnorms[3*triangle->findex+2]);
                            verts.push_back(0.0f);
                        }
					}
					if (btexcoord)
					{
						verts.push_back(data_->texcoords[2*triangle->tindices[j]]);
						verts.push_back(data_->texcoords[2*triangle->tindices[j]+1]);
					}
					ids.push_back(ids.size());
				}
                
                if (verts.size() * 4ULL > 1610612720ULL)
                {
                    MeshVertexBuffers v;
                    if (!verts.empty() && !ids.empty())
                    {
                        VK_CHECK_RESULT(device_->createBufferInPool(
                            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                            &v.vertexBuffer,
                            verts.size() * sizeof(float)));
                        device_->UploadData2Buffer(verts.data(), &v.vertexBuffer, 0, verts.size() * sizeof(float));

                        VK_CHECK_RESULT(device_->createBufferInPool(
                            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                            &v.indexBuffer,
                            ids.size() * sizeof(uint32_t)));
                        device_->UploadData2Buffer(ids.data(), &v.indexBuffer, 0, ids.size() * sizeof(uint32_t));
                    }
                    v.indexCount = ids.size();
                    m_vertbufs.push_back(v);
                    verts.clear();
                    ids.clear();
                }
			}
			group = group->next;
		}

		MeshVertexBuffers v;
		if (!verts.empty() && !ids.empty())
		{
			VK_CHECK_RESULT(device_->createBufferInPool(
				VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				&v.vertexBuffer,
				verts.size() * sizeof(float)));
			device_->UploadData2Buffer(verts.data(), &v.vertexBuffer, 0, verts.size() * sizeof(float));

			VK_CHECK_RESULT(device_->createBufferInPool(
				VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				&v.indexBuffer,
				ids.size() * sizeof(uint32_t)));
			device_->UploadData2Buffer(ids.data(), &v.indexBuffer, 0, ids.size() * sizeof(uint32_t));
		}
		v.indexCount = ids.size();
		m_vertbufs.push_back(v);

		update_ = false;
	}

	void MeshRenderer::draw(const std::unique_ptr<vks::VFrameBuffer>& framebuf, bool clear_framebuf)
	{
		if (!data_ || !data_->vertices || !data_->triangles)
			return;

		//set up vertex array object
		if (update_)
		{
			update();
		}

		bool destroy_buffers = false;
		if (device_->available_mem < 0)
			destroy_buffers = true;

		bool tex = data_->hastexture || (data_->groups && data_->groups->next);

		MeshPipeline pipeline = prepareMeshPipeline(device_, 0, MSHRENDER_BLEND_DISABLE, tex);
		VkPipelineLayout pipelineLayout = m_vulkan->msh_shader_factory_->pipeline_[device_].pipelineLayout;

		framebuf->replaceRenderPass(pipeline.renderpass);
		
		VkCommandBuffer cmdbuf = device_->GetNextCommandBuffer();

		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
		cmdBufInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
		renderPassBeginInfo.renderPass = pipeline.renderpass;
		renderPassBeginInfo.renderArea.offset.x = 0;
		renderPassBeginInfo.renderArea.offset.y = 0;
		renderPassBeginInfo.renderArea.extent.width = framebuf->w;
		renderPassBeginInfo.renderArea.extent.height = framebuf->h;
		renderPassBeginInfo.framebuffer = framebuf->framebuffer;

		VK_CHECK_RESULT(vkBeginCommandBuffer(cmdbuf, &cmdBufInfo));

		size_t anum = framebuf->attachments.size();
		//layout transition
		if (depth_peel_)
		{
			vks::tools::setImageLayout(
				cmdbuf,
				m_depth_tex->image,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
				m_depth_tex->subresourceRange);
			m_depth_tex->descriptor.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
		}

		vkCmdBeginRenderPass(cmdbuf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		VkViewport viewport = vks::initializers::viewport((float)framebuf->w, (float)framebuf->h, 0.0f, 1.0f);
		vkCmdSetViewport(cmdbuf, 0, 1, &viewport);

		VkRect2D scissor = vks::initializers::rect2D(framebuf->w, framebuf->h, 0, 0);
		vkCmdSetScissor(cmdbuf, 0, 1, &scissor);

		vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.vkpipeline);

		if (clear_framebuf)
		{
			vector<VkClearAttachment> clearAttachments;
			clearAttachments.resize(anum);
			for (int i = 0; i < anum-1; i++)
			{
				clearAttachments[i].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				clearAttachments[i].clearValue = {0.0f, 0.0f, 0.0f, 0.0f};
				clearAttachments[i].colorAttachment = i;
			}
			clearAttachments[anum-1].aspectMask = framebuf->attachments[anum - 1]->subresourceRange.aspectMask;
			clearAttachments[anum-1].clearValue = { 1.0f, 0.0f, 0.0f, 0.0f };
			clearAttachments[anum-1].colorAttachment = 0;

			VkClearRect clearRect[1] = {};
			clearRect[0].layerCount = 1;
			clearRect[0].rect.offset = { 0, 0 };
			clearRect[0].rect.extent = { framebuf->w, framebuf->h };

			vkCmdClearAttachments(
				cmdbuf,
				anum,
				clearAttachments.data(),
				1,
				clearRect);
		}

		MshShaderFactory::MshVertShaderUBO vubo;
		MshShaderFactory::MshFragShaderUBO fubo;

		vubo.proj_mat = m_proj_mat;
		vubo.mv_mat = m_mv_mat;
		if (light_ && (data_->normals || data_->facetnorms))
			vubo.normal_mat = glm::mat4(glm::inverseTranspose(glm::mat3(m_mv_mat)));
        vubo.threshold = threshold_;

		if (fog_)
			fubo.loc8 = { m_fog_intensity, m_fog_start, m_fog_end, 0.0f };

		if (depth_peel_)
			fubo.loc7 = { 1.0f / float(framebuf->w), 1.0f / float(framebuf->h), 0.0f, 0.0f };

		BBox dbox = bounds_;
		glm::mat4 gmat = glm::mat4(float(dbox.max().x() - dbox.min().x()), 0.0f, 0.0f, float(dbox.min().x()),
			0.0f, float(dbox.max().y() - dbox.min().y()), 0.0f, float(dbox.min().y()),
			0.0f, 0.0f, -float(dbox.max().z() - dbox.min().z()), float(dbox.max().z()),
			0.0f, 0.0f, 0.0f, 1.0f);
		glm::mat4 invmat = glm::inverseTranspose(gmat);
		fubo.matrix3 = invmat;

		//set clipping planes
		double abcd[4];
		planes_[0]->get(abcd);
		fubo.plane0 = { abcd[0], abcd[1], abcd[2], abcd[3] };
		planes_[1]->get(abcd);
		fubo.plane1 = { abcd[0], abcd[1], abcd[2], abcd[3] };
		planes_[2]->get(abcd);
		fubo.plane2 = { abcd[0], abcd[1], abcd[2], abcd[3] };
		planes_[3]->get(abcd);
		fubo.plane3 = { abcd[0], abcd[1], abcd[2], abcd[3] };
		planes_[4]->get(abcd);
		fubo.plane4 = { abcd[0], abcd[1], abcd[2], abcd[3] };
		planes_[5]->get(abcd);
		fubo.plane5 = { abcd[0], abcd[1], abcd[2], abcd[3] };

		GLMgroup* group = data_->groups;
		//int count = 0;
		//while (group)
		for (int count = 0; count < m_vertbufs.size(); count++)
		{
			if (m_vertbufs[count].indexCount == 0)
			{
				//group = group->next;
				//count++;
				continue;
			}

			vks::Buffer vert_ubuf, frag_ubuf;
			VkDeviceSize vert_ubuf_offset, frag_ubuf_offset;
			device_->GetNextUniformBuffer(sizeof(MshShaderFactory::MshVertShaderUBO), vert_ubuf, vert_ubuf_offset);
			device_->GetNextUniformBuffer(sizeof(MshShaderFactory::MshFragShaderUBO), frag_ubuf, frag_ubuf_offset);

			std::vector<VkWriteDescriptorSet> descriptorWritesBase;
			m_vulkan->msh_shader_factory_->getDescriptorSetWriteUniforms(device_, vert_ubuf, frag_ubuf, descriptorWritesBase);

			//uniforms
			if (light_ && (data_->normals || data_->facetnorms))
			{
				GLMmaterial* material = &data_->materials[group->material];
				if (material)
				{
					fubo.loc0 = { material->ambient[0], material->ambient[1], material->ambient[2], material->ambient[3] };
					fubo.loc1 = { material->diffuse[0], material->diffuse[1], material->diffuse[2], material->diffuse[3] };
					fubo.loc2 = { material->specular[0], material->specular[1], material->specular[2], material->specular[3] };
					fubo.loc3 = { material->shininess, alpha_, 0.0f, 0.0f };
				}
			}
			else
			{//color
				GLMmaterial* material = &data_->materials[group->material];
				if (material)
					fubo.loc0 = { material->diffuse[0], material->diffuse[1], material->diffuse[2], material->diffuse[3] };
				else
					fubo.loc0 = { 1.0f, 0.0f, 0.0f, 1.0f };//color
				fubo.loc3 = { 0.0f, alpha_, 0.0f, 0.0f };//alpha
			}
			//This won't happen now.
			//if (tex && data_->texcoords)
			//{
				//GLMmaterial* material = &data_->materials[group->material];
				//descriptorWritesBase.push_back(MshShaderFactory::writeDescriptorSetTex(VK_NULL_HANDLE, 0, material->textureID))
			//}
            if (group->next) //multiple groups
            {
                auto palette = get_palette();
                if (palette.count(device_) > 0)
                    descriptorWritesBase.push_back(MshShaderFactory::writeDescriptorSetTex(VK_NULL_HANDLE, 0, &palette[device_]->descriptor));
            }
			if (depth_peel_)
			{
				descriptorWritesBase.push_back(MshShaderFactory::writeDescriptorSetTex(VK_NULL_HANDLE, 1, &m_depth_tex->descriptor));
			}

			vert_ubuf.copyTo(&vubo, sizeof(MshShaderFactory::MshVertShaderUBO), vert_ubuf_offset);
			frag_ubuf.copyTo(&fubo, sizeof(MshShaderFactory::MshFragShaderUBO), frag_ubuf_offset);
			if (vert_ubuf.buffer == frag_ubuf.buffer)
				vert_ubuf.flush(device_->GetCurrentUniformBufferOffset() - vert_ubuf_offset, vert_ubuf_offset);
			else
			{
				if (vert_ubuf_offset == 0)
					vert_ubuf.flush();
				else
					vert_ubuf.flush(VK_WHOLE_SIZE, vert_ubuf_offset);
				frag_ubuf.flush();
			}

			if (!descriptorWritesBase.empty())
			{
				device_->vkCmdPushDescriptorSetKHR(
					cmdbuf,
					VK_PIPELINE_BIND_POINT_GRAPHICS,
					pipelineLayout,
					0,
					descriptorWritesBase.size(),
					descriptorWritesBase.data()
				);
			}

			VkDeviceSize offsets[1] = { 0 };
			vkCmdBindVertexBuffers(cmdbuf, 0, 1, &m_vertbufs[count].vertexBuffer.buffer, offsets);
			vkCmdBindIndexBuffer(cmdbuf, m_vertbufs[count].indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(cmdbuf, m_vertbufs[count].indexCount, 1, 0, 0, 0);

			//group = group->next;
			//count++;
		}

		vkCmdEndRenderPass(cmdbuf);

		//layout transition
		if (depth_peel_)
		{
			m_depth_tex->descriptor.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			vks::tools::setImageLayout(
				cmdbuf,
				m_depth_tex->image,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
				m_depth_tex->subresourceRange);
		}

		VK_CHECK_RESULT(vkEndCommandBuffer(cmdbuf));

		vks::VulkanSemaphoreSettings semaphores = device_->GetNextRenderSemaphoreSettings();

		VkSubmitInfo submitInfo = vks::initializers::submitInfo();
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &cmdbuf;
		submitInfo.signalSemaphoreCount = semaphores.signalSemaphoreCount;
		submitInfo.pSignalSemaphores = semaphores.signalSemaphores;

		std::vector<VkPipelineStageFlags> waitStages;
		if (semaphores.waitSemaphoreCount > 0)
		{
			for (uint32_t i = 0; i < semaphores.waitSemaphoreCount; i++)
				waitStages.push_back(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
			submitInfo.waitSemaphoreCount = semaphores.waitSemaphoreCount;
			submitInfo.pWaitSemaphores = semaphores.waitSemaphores;
			submitInfo.pWaitDstStageMask = waitStages.data();
		}

		// Submit to the queue
		VK_CHECK_RESULT(vkQueueSubmit(device_->queue, 1, &submitInfo, VK_NULL_HANDLE));
		
		if (destroy_buffers) {
			vkQueueWaitIdle(device_->queue);
			ReleaseVertexBuffers();
		}
	}

	void MeshRenderer::draw_wireframe(const std::unique_ptr<vks::VFrameBuffer>& framebuf, bool clear_framebuf)
	{
		if (!data_ || !data_->vertices || !data_->triangles)
			return;

		//set up vertex array object
		if (update_)
		{
			update();
			update_ = false;
		}

        bool tex = data_->hastexture || (data_->groups && data_->groups->next);
        int type = (data_->groups && data_->groups->next) ? 3 : 0;

		MeshPipeline pipeline = prepareMeshPipeline(device_, type, MSHRENDER_BLEND_OVER, tex, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_POLYGON_MODE_LINE);
		VkPipelineLayout pipelineLayout = m_vulkan->msh_shader_factory_->pipeline_[device_].pipelineLayout;

		framebuf->replaceRenderPass(pipeline.renderpass);

		VkCommandBuffer cmdbuf = device_->GetNextCommandBuffer();

		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
		cmdBufInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
		renderPassBeginInfo.renderPass = pipeline.renderpass;
		renderPassBeginInfo.renderArea.offset.x = 0;
		renderPassBeginInfo.renderArea.offset.y = 0;
		renderPassBeginInfo.renderArea.extent.width = framebuf->w;
		renderPassBeginInfo.renderArea.extent.height = framebuf->h;
		renderPassBeginInfo.framebuffer = framebuf->framebuffer;

		VK_CHECK_RESULT(vkBeginCommandBuffer(cmdbuf, &cmdBufInfo));

		//layout transition
		if (depth_peel_)
		{
			vks::tools::setImageLayout(
				cmdbuf,
				m_depth_tex->image,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
				m_depth_tex->subresourceRange);
			m_depth_tex->descriptor.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
		}

		vkCmdBeginRenderPass(cmdbuf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		VkViewport viewport = vks::initializers::viewport((float)framebuf->w, (float)framebuf->h, 0.0f, 1.0f);
		vkCmdSetViewport(cmdbuf, 0, 1, &viewport);

		VkRect2D scissor = vks::initializers::rect2D(framebuf->w, framebuf->h, 0, 0);
		vkCmdSetScissor(cmdbuf, 0, 1, &scissor);

		vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.vkpipeline);

		size_t anum = framebuf->attachments.size();
		if (clear_framebuf)
		{
			vector<VkClearAttachment> clearAttachments;
			clearAttachments.resize(anum);
			for (int i = 0; i < anum - 1; i++)
			{
				clearAttachments[i].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				clearAttachments[i].clearValue = { 0.0f, 0.0f, 0.0f, 0.0f };
				clearAttachments[i].colorAttachment = i;
			}
			clearAttachments[anum - 1].aspectMask = framebuf->attachments[anum - 1]->subresourceRange.aspectMask;
			clearAttachments[anum - 1].clearValue = { 1.0f, 0.0f, 0.0f, 0.0f };
			clearAttachments[anum - 1].colorAttachment = 0;

			VkClearRect clearRect[1] = {};
			clearRect[0].layerCount = 1;
			clearRect[0].rect.offset = { 0, 0 };
			clearRect[0].rect.extent = { framebuf->w, framebuf->h };

			vkCmdClearAttachments(
				cmdbuf,
				anum,
				clearAttachments.data(),
				1,
				clearRect);
		}

		MshShaderFactory::MshVertShaderUBO vubo;
		MshShaderFactory::MshFragShaderUBO fubo;

		vubo.proj_mat = m_proj_mat;
		vubo.mv_mat = m_mv_mat;
        vubo.threshold = threshold_;
		GLMmaterial* material = &data_->materials[0];
		if (material)
			fubo.loc0 = { material->diffuse[0], material->diffuse[1], material->diffuse[2], material->diffuse[3] };
		else
			fubo.loc0 = { 1.0f, 0.0f, 0.0f, 1.0f };//color
		fubo.loc3 = { 0.0f, alpha_, 0.0f, 0.0f };//alpha
		if (fog_)
			fubo.loc8 = { m_fog_intensity, m_fog_start, m_fog_end, 0.0f };

		if (depth_peel_)
			fubo.loc7 = { 1.0f / float(framebuf->w), 1.0f / float(framebuf->h), 0.0f, 0.0f };

		BBox dbox = bounds_;
        glm::mat4 gmat = glm::mat4(float(dbox.max().x() - dbox.min().x()), 0.0f, 0.0f, float(dbox.min().x()),
                                   0.0f, float(dbox.max().y() - dbox.min().y()), 0.0f, float(dbox.min().y()),
                                   0.0f, 0.0f, -float(dbox.max().z() - dbox.min().z()), float(dbox.max().z()),
                                   0.0f, 0.0f, 0.0f, 1.0f);
		glm::mat4 invmat = glm::inverseTranspose(gmat);
		fubo.matrix3 = invmat;

		//set clipping planes
		double abcd[4];
		planes_[0]->get(abcd);
		fubo.plane0 = { abcd[0], abcd[1], abcd[2], abcd[3] };
		planes_[1]->get(abcd);
		fubo.plane1 = { abcd[0], abcd[1], abcd[2], abcd[3] };
		planes_[2]->get(abcd);
		fubo.plane2 = { abcd[0], abcd[1], abcd[2], abcd[3] };
		planes_[3]->get(abcd);
		fubo.plane3 = { abcd[0], abcd[1], abcd[2], abcd[3] };
		planes_[4]->get(abcd);
		fubo.plane4 = { abcd[0], abcd[1], abcd[2], abcd[3] };
		planes_[5]->get(abcd);
		fubo.plane5 = { abcd[0], abcd[1], abcd[2], abcd[3] };

		vks::Buffer vert_ubuf, frag_ubuf;
		VkDeviceSize vert_ubuf_offset, frag_ubuf_offset;
		device_->GetNextUniformBuffer(sizeof(MshShaderFactory::MshVertShaderUBO), vert_ubuf, vert_ubuf_offset);
		device_->GetNextUniformBuffer(sizeof(MshShaderFactory::MshFragShaderUBO), frag_ubuf, frag_ubuf_offset);

		std::vector<VkWriteDescriptorSet> descriptorWritesBase;
		m_vulkan->msh_shader_factory_->getDescriptorSetWriteUniforms(device_, vert_ubuf, frag_ubuf, descriptorWritesBase);

		vert_ubuf.copyTo(&vubo, sizeof(MshShaderFactory::MshVertShaderUBO), vert_ubuf_offset);
		frag_ubuf.copyTo(&fubo, sizeof(MshShaderFactory::MshFragShaderUBO), frag_ubuf_offset);
		if (vert_ubuf.buffer == frag_ubuf.buffer)
			vert_ubuf.flush(device_->GetCurrentUniformBufferOffset() - vert_ubuf_offset, vert_ubuf_offset);
		else
		{
			if (vert_ubuf_offset == 0)
				vert_ubuf.flush();
			else
				vert_ubuf.flush(VK_WHOLE_SIZE, vert_ubuf_offset);
			frag_ubuf.flush();
		}
        
        if (data_->groups && data_->groups->next) //multiple groups
        {
            auto palette = get_palette();
            if (palette.count(device_) > 0)
                descriptorWritesBase.push_back(MshShaderFactory::writeDescriptorSetTex(VK_NULL_HANDLE, 0, &palette[device_]->descriptor));
            auto sel_palette = get_selection_palette();
            if (sel_palette.count(device_) > 0)
                descriptorWritesBase.push_back(MshShaderFactory::writeDescriptorSetTex(VK_NULL_HANDLE, 2, &sel_palette[device_]->descriptor));
        }
        if (depth_peel_)
        {
            descriptorWritesBase.push_back(MshShaderFactory::writeDescriptorSetTex(VK_NULL_HANDLE, 1, &m_depth_tex->descriptor));
        }

		if (!descriptorWritesBase.empty())
		{
			device_->vkCmdPushDescriptorSetKHR(
				cmdbuf,
				VK_PIPELINE_BIND_POINT_GRAPHICS,
				pipelineLayout,
				0,
				descriptorWritesBase.size(),
				descriptorWritesBase.data()
			);
		}

		GLMgroup* group = data_->groups;
		//int count = 0;
		//while (group)
		for (int count = 0; count < m_vertbufs.size(); count++)
		{
			if (m_vertbufs[count].indexCount == 0)
			{
				//group = group->next;
				//count++;
				continue;
			}

			VkDeviceSize offsets[1] = { 0 };
			vkCmdBindVertexBuffers(cmdbuf, 0, 1, &m_vertbufs[count].vertexBuffer.buffer, offsets);
			vkCmdBindIndexBuffer(cmdbuf, m_vertbufs[count].indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(cmdbuf, m_vertbufs[count].indexCount, 1, 0, 0, 0);

			//group = group->next;
			//count++;
		}

		vkCmdEndRenderPass(cmdbuf);

		//layout transition
		if (depth_peel_)
		{
			m_depth_tex->descriptor.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			vks::tools::setImageLayout(
				cmdbuf,
				m_depth_tex->image,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
				m_depth_tex->subresourceRange);
		}

		VK_CHECK_RESULT(vkEndCommandBuffer(cmdbuf));

		vks::VulkanSemaphoreSettings semaphores = device_->GetNextRenderSemaphoreSettings();

		VkSubmitInfo submitInfo = vks::initializers::submitInfo();
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &cmdbuf;
		submitInfo.signalSemaphoreCount = semaphores.signalSemaphoreCount;
		submitInfo.pSignalSemaphores = semaphores.signalSemaphores;

		std::vector<VkPipelineStageFlags> waitStages;
		if (semaphores.waitSemaphoreCount > 0)
		{
			for (uint32_t i = 0; i < semaphores.waitSemaphoreCount; i++)
				waitStages.push_back(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
			submitInfo.waitSemaphoreCount = semaphores.waitSemaphoreCount;
			submitInfo.pWaitSemaphores = semaphores.waitSemaphores;
			submitInfo.pWaitDstStageMask = waitStages.data();
		}

		// Submit to the queue
		VK_CHECK_RESULT(vkQueueSubmit(device_->queue, 1, &submitInfo, VK_NULL_HANDLE));

	}

	void MeshRenderer::draw_integer(unsigned int name, const std::unique_ptr<vks::VFrameBuffer>& framebuf, bool clear_framebuf, VkRect2D scissor, bool segment)
	{
		if (!data_ || !data_->vertices || !data_->triangles)
			return;

		//set up vertex array object
		if (update_)
		{
			update();
			update_ = false;
		}

		bool tex = data_->hastexture;

		MeshPipeline pipeline = prepareMeshPipeline(device_, segment ? 2 : 1, MSHRENDER_BLEND_DISABLE, false);
		VkPipelineLayout pipelineLayout = m_vulkan->msh_shader_factory_->pipeline_[device_].pipelineLayout;

		framebuf->replaceRenderPass(pipeline.renderpass);

		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
		cmdBufInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
		renderPassBeginInfo.renderPass = pipeline.renderpass;
		renderPassBeginInfo.renderArea.offset.x = 0;
		renderPassBeginInfo.renderArea.offset.y = 0;
		renderPassBeginInfo.renderArea.extent.width = framebuf->w;
		renderPassBeginInfo.renderArea.extent.height = framebuf->h;
		renderPassBeginInfo.framebuffer = framebuf->framebuffer;
		
		VkCommandBuffer cmdbuf = device_->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

		vkCmdBeginRenderPass(cmdbuf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		VkViewport viewport = vks::initializers::viewport((float)framebuf->w, (float)framebuf->h, 0.0f, 1.0f);
		vkCmdSetViewport(cmdbuf, 0, 1, &viewport);

		if (scissor.extent.width == 0 || scissor.extent.height == 0)
			scissor = vks::initializers::rect2D(framebuf->w, framebuf->h, 0, 0);
		vkCmdSetScissor(cmdbuf, 0, 1, &scissor);

		vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.vkpipeline);

		size_t anum = framebuf->attachments.size();
		if (clear_framebuf)
		{
			vector<VkClearAttachment> clearAttachments;
			clearAttachments.resize(anum);
			for (int i = 0; i < anum - 1; i++)
			{
				clearAttachments[i].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				clearAttachments[i].clearValue = { 0.0f, 0.0f, 0.0f, 0.0f };
				clearAttachments[i].colorAttachment = i;
			}
			clearAttachments[anum - 1].aspectMask = framebuf->attachments[anum - 1]->subresourceRange.aspectMask;
			clearAttachments[anum - 1].clearValue = { 1.0f, 0.0f, 0.0f, 0.0f };
			clearAttachments[anum - 1].colorAttachment = 0;

			VkClearRect clearRect[1] = {};
			clearRect[0].layerCount = 1;
			clearRect[0].rect.offset = { 0, 0 };
			clearRect[0].rect.extent = { framebuf->w, framebuf->h };

			vkCmdClearAttachments(
				cmdbuf,
				anum,
				clearAttachments.data(),
				1,
				clearRect);
		}

		MshShaderFactory::MshVertShaderUBO vubo;
		MshShaderFactory::MshFragShaderUBO fubo;

		vubo.proj_mat = m_proj_mat;
		vubo.mv_mat = m_mv_mat;
        vubo.threshold = threshold_;
		GLMmaterial* material = &data_->materials[0];
		fubo.loci0 = name;

		//set clipping planes
		double abcd[4];
		planes_[0]->get(abcd);
		fubo.plane0 = { abcd[0], abcd[1], abcd[2], abcd[3] };
		planes_[1]->get(abcd);
		fubo.plane1 = { abcd[0], abcd[1], abcd[2], abcd[3] };
		planes_[2]->get(abcd);
		fubo.plane2 = { abcd[0], abcd[1], abcd[2], abcd[3] };
		planes_[3]->get(abcd);
		fubo.plane3 = { abcd[0], abcd[1], abcd[2], abcd[3] };
		planes_[4]->get(abcd);
		fubo.plane4 = { abcd[0], abcd[1], abcd[2], abcd[3] };
		planes_[5]->get(abcd);
		fubo.plane5 = { abcd[0], abcd[1], abcd[2], abcd[3] };

		BBox dbox = bounds_;
        glm::mat4 gmat = glm::mat4(float(dbox.max().x() - dbox.min().x()), 0.0f, 0.0f, float(dbox.min().x()),
                                   0.0f, float(dbox.max().y() - dbox.min().y()), 0.0f, float(dbox.min().y()),
                                   0.0f, 0.0f, -float(dbox.max().z() - dbox.min().z()), float(dbox.max().z()),
                                   0.0f, 0.0f, 0.0f, 1.0f);
		glm::mat4 invmat = glm::inverseTranspose(gmat);
		fubo.matrix3 = invmat;

		vks::Buffer vert_ubuf, frag_ubuf;
		VkDeviceSize vert_ubuf_offset, frag_ubuf_offset;
		device_->GetNextUniformBuffer(sizeof(MshShaderFactory::MshVertShaderUBO), vert_ubuf, vert_ubuf_offset);
		device_->GetNextUniformBuffer(sizeof(MshShaderFactory::MshFragShaderUBO), frag_ubuf, frag_ubuf_offset);

		std::vector<VkWriteDescriptorSet> descriptorWritesBase;
		m_vulkan->msh_shader_factory_->getDescriptorSetWriteUniforms(device_, vert_ubuf, frag_ubuf, descriptorWritesBase);

		vert_ubuf.copyTo(&vubo, sizeof(MshShaderFactory::MshVertShaderUBO), vert_ubuf_offset);
		frag_ubuf.copyTo(&fubo, sizeof(MshShaderFactory::MshFragShaderUBO), frag_ubuf_offset);
		if (vert_ubuf.buffer == frag_ubuf.buffer)
			vert_ubuf.flush(device_->GetCurrentUniformBufferOffset() - vert_ubuf_offset, vert_ubuf_offset);
		else
		{
			if (vert_ubuf_offset == 0)
				vert_ubuf.flush();
			else
				vert_ubuf.flush(VK_WHOLE_SIZE, vert_ubuf_offset);
			frag_ubuf.flush();
		}
		
		if (!descriptorWritesBase.empty())
		{
			device_->vkCmdPushDescriptorSetKHR(
				cmdbuf,
				VK_PIPELINE_BIND_POINT_GRAPHICS,
				pipelineLayout,
				0,
				descriptorWritesBase.size(),
				descriptorWritesBase.data()
			);
		}

		GLMgroup* group = data_->groups;
		//int count = 0;
		//while (group)
		for (int count = 0; count < m_vertbufs.size(); count++)
		{
			if (m_vertbufs[count].indexCount == 0)
			{
				//group = group->next;
				//count++;
				continue;
			}

			VkDeviceSize offsets[1] = { 0 };
			vkCmdBindVertexBuffers(cmdbuf, 0, 1, &m_vertbufs[count].vertexBuffer.buffer, offsets);
			vkCmdBindIndexBuffer(cmdbuf, m_vertbufs[count].indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(cmdbuf, m_vertbufs[count].indexCount, 1, 0, 0, 0);

			//group = group->next;
			//count++;
		}

		vkCmdEndRenderPass(cmdbuf);

		device_->flushCommandBuffer(cmdbuf, device_->queue, true);

	}

} // namespace FLIVR
