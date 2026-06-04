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

#include <string>
#include <sstream>
#include <FLIVR/VolWarpShader.h>
#include <FLIVR/ShaderProgram.h>

using std::string;
using std::vector;
using std::ostringstream;

namespace FLIVR
{
	// local size
	#define WARP_INPUTS \
		"layout (local_size_x = 4, local_size_y = 4, local_size_z = 4) in;\n"

	// output storage image (format chosen by output bytes)
	#define WARP_OUT_8BIT  "layout (binding = 0, r8) uniform image3D outimg;\n"
	#define WARP_OUT_16BIT "layout (binding = 0, r16) uniform image3D outimg;\n"
	#define WARP_OUT_32BIT "layout (binding = 0, r32f) uniform image3D outimg;\n"

	// bindings 1-3, helpers and main(): numerically invert the forward TPS
	// (Gauss-Newton + backtracking line search) per output voxel.
	static const char* WARP_BODY =
		"layout (binding = 1) uniform sampler3D srctile;//moving source sub-region\n"
		"layout (std430, binding = 2) readonly buffer LM { vec4 lm[]; };//2 vec4/landmark: src_i, W_i\n"
		"layout (binding = 3) uniform Warp {\n"
		"	mat4 A;    //forward affine (b in column 3)\n"
		"	mat4 Ainv; //inverse affine f->m\n"
		"	ivec4 cfg; //(numLandmarks, maxIter, lineSearchTries, 0)\n"
		"	vec4 prm;  //(eps, beta, cArmijo, 0)\n"
		"} u;\n"
		"layout (push_constant) uniform PC {\n"
		"	vec4 volDimInv;\n"
		"	ivec4 brickOrigin;\n"
		"	ivec4 validDims;\n"
		"	vec4 tileOrigin;\n"
		"	vec4 tileSizeInv;\n"
		"} pc;\n"
		"\n"
		"vec3 Fwd(vec3 m){\n"
		"	vec3 r = (u.A*vec4(m,1.0)).xyz;\n"
		"	int n = u.cfg.x;\n"
		"	for(int i=0;i<n;i++){\n"
		"		vec3 d = m - lm[2*i].xyz; float r2 = dot(d,d);\n"
		"		r += lm[2*i+1].xyz * (r2>1e-12 ? 0.5*r2*log(r2) : 0.0);\n"
		"	}\n"
		"	return r;\n"
		"}\n"
		"mat3 Jac(vec3 m){\n"
		"	mat3 J = mat3(u.A);\n"
		"	int n = u.cfg.x;\n"
		"	for(int i=0;i<n;i++){\n"
		"		vec3 d = m - lm[2*i].xyz; float r2 = dot(d,d);\n"
		"		if(r2>1e-12){ float k = log(r2)+1.0; vec3 w = lm[2*i+1].xyz;\n"
		"			J[0]+=w*(d.x*k); J[1]+=w*(d.y*k); J[2]+=w*(d.z*k); }\n"
		"	}\n"
		"	return J;\n"
		"}\n"
		"void main(){\n"
		"	ivec3 gid = ivec3(gl_GlobalInvocationID.xyz);\n"
		"	if(any(greaterThanEqual(gid, pc.validDims.xyz))) return;\n"
		"	vec3 f = (vec3(pc.brickOrigin.xyz + gid) + 0.5) * pc.volDimInv.xyz;\n"
		"	vec3 m = (u.Ainv*vec4(f,1.0)).xyz;\n"
		"	int maxit = u.cfg.y; int lstries = u.cfg.z;\n"
		"	float eps = u.prm.x; float beta = u.prm.y; float carm = u.prm.z;\n"
		"	for(int it=0; it<maxit; ++it){\n"
		"		vec3 err = f - Fwd(m);\n"
		"		if(max(max(abs(err.x),abs(err.y)),abs(err.z)) < eps) break;\n"
		"		mat3 J = Jac(m);\n"
		"		if(abs(determinant(J)) < 1e-12) break;\n"
		"		vec3 dir = inverse(J)*err;\n"
		"		float c0 = 0.5*dot(err,err);\n"
		"		vec3 gC = -transpose(J)*err;\n"
		"		float md = dot(gC, dir);\n"
		"		float t = 1.0;\n"
		"		for(int ls=0; ls<lstries; ++ls){\n"
		"			vec3 e2 = f - Fwd(m + t*dir);\n"
		"			if(0.5*dot(e2,e2) <= c0 + carm*t*md) break;\n"
		"			t *= beta;\n"
		"		}\n"
		"		m += t*dir;\n"
		"	}\n"
		"	vec3 tc = (m - pc.tileOrigin.xyz) * pc.tileSizeInv.xyz;\n"
		"	float v = texture(srctile, clamp(tc, 0.0, 1.0)).r;\n"
		"	imageStore(outimg, gid, vec4(v));\n"
		"}\n";

	VolWarpShader::VolWarpShader(VkDevice device, int out_bytes) :
		device_(device),
		out_bytes_(out_bytes),
		program_(0)
	{}

	VolWarpShader::~VolWarpShader()
	{
		delete program_;
	}

	bool VolWarpShader::create()
	{
		string cs;
		if (emit(cs)) return true;
		program_ = new ShaderProgram(cs);
		program_->create(device_);
		return false;
	}

	bool VolWarpShader::emit(string& s)
	{
		ostringstream z;

		z << ShaderProgram::glsl_version_;
		z << WARP_INPUTS;
		if (out_bytes_ == 2) z << WARP_OUT_16BIT;
		else if (out_bytes_ == 4) z << WARP_OUT_32BIT;
		else z << WARP_OUT_8BIT;
		z << WARP_BODY;

		s = z.str();
		return false;
	}

	VolWarpShaderFactory::VolWarpShaderFactory()
		: prev_shader_(-1)
	{}

	VolWarpShaderFactory::VolWarpShaderFactory(std::vector<vks::VulkanDevice*>& devices)
		: prev_shader_(-1)
	{
		init(devices);
	}

	void VolWarpShaderFactory::init(std::vector<vks::VulkanDevice*>& devices)
	{
		vdevices_ = devices;
		setupDescriptorSetLayout();
	}

	VolWarpShaderFactory::~VolWarpShaderFactory()
	{
		for (unsigned int i = 0; i < shader_.size(); i++)
		{
			delete shader_[i];
		}

		for (auto vdev : vdevices_)
		{
			VkDevice device = vdev->logicalDevice;

			vkDestroyPipelineLayout(device, pipeline_[vdev].pipelineLayout, nullptr);
			vkDestroyDescriptorSetLayout(device, pipeline_[vdev].descriptorSetLayout, nullptr);
		}
	}

	ShaderProgram* VolWarpShaderFactory::shader(VkDevice device, int out_bytes)
	{
		if (prev_shader_ >= 0)
		{
			if (shader_[prev_shader_]->match(device, out_bytes))
			{
				return shader_[prev_shader_]->program();
			}
		}
		for (unsigned int i = 0; i < shader_.size(); i++)
		{
			if (shader_[i]->match(device, out_bytes))
			{
				prev_shader_ = i;
				return shader_[i]->program();
			}
		}

		VolWarpShader* s = new VolWarpShader(device, out_bytes);
		if (s->create())
		{
			delete s;
			return 0;
		}
		shader_.push_back(s);
		prev_shader_ = int(shader_.size()) - 1;
		return s->program();
	}

	void VolWarpShaderFactory::setupDescriptorSetLayout()
	{
		for (auto vdev : vdevices_)
		{
			VkDevice device = vdev->logicalDevice;

			std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
				// binding 0: output storage image
				vks::initializers::descriptorSetLayoutBinding(
					VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					VK_SHADER_STAGE_COMPUTE_BIT,
					0),
				// binding 1: source tile sampler
				vks::initializers::descriptorSetLayoutBinding(
					VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					VK_SHADER_STAGE_COMPUTE_BIT,
					1),
				// binding 2: landmark storage buffer
				vks::initializers::descriptorSetLayoutBinding(
					VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
					VK_SHADER_STAGE_COMPUTE_BIT,
					2),
				// binding 3: transform uniform buffer
				vks::initializers::descriptorSetLayoutBinding(
					VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					VK_SHADER_STAGE_COMPUTE_BIT,
					3),
			};

			VkDescriptorSetLayoutCreateInfo descriptorLayout =
				vks::initializers::descriptorSetLayoutCreateInfo(
					setLayoutBindings.data(),
					static_cast<uint32_t>(setLayoutBindings.size()));

			descriptorLayout.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
			descriptorLayout.pNext = nullptr;

			VolWarpPipeline pipe;
			VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &pipe.descriptorSetLayout));

			VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
				vks::initializers::pipelineLayoutCreateInfo(
					&pipe.descriptorSetLayout,
					1);

			VkPushConstantRange pushConstantRange = {};
			pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
			pushConstantRange.size = sizeof(WarpCompShaderBrickConst);
			pushConstantRange.offset = 0;

			pPipelineLayoutCreateInfo.pushConstantRangeCount = 1;
			pPipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;

			VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &pipe.pipelineLayout));

			pipeline_[vdev] = pipe;
		}
	}

} // end namespace FLIVR
