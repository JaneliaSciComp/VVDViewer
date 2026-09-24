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

	// bindings 1-3, helpers and main(): evaluate the resampling map (fixed ->
	// moving, as BigWarp) directly per output voxel, then sample the source
	// like imglib2 (nearest / N-linear, zero outside the volume).
	static const char* WARP_BODY =
		"layout (binding = 1) uniform sampler3D srctile;//moving source sub-region (texelFetch only)\n"
		"layout (std430, binding = 2) readonly buffer LM { vec4 lm[]; };//2 vec4/landmark: knot k_i, weight W_i\n"
		"layout (binding = 3) uniform Warp {\n"
		"	mat4 G;       //affine part, source voxel coords: p = G*(u,1)\n"
		"	vec4 gscale;  //u = o*gscale + goff (o = output voxel index)\n"
		"	vec4 goff;\n"
		"	ivec4 srcDim; //source volume dims (voxels)\n"
		"	ivec4 cfg;    //(numLandmarks, interp: 0 nearest / 1 linear, 0, 0)\n"
		"} xf;\n"
		"layout (push_constant) uniform PC {\n"
		"	ivec4 brickOrigin;\n"
		"	ivec4 validDims;\n"
		"	ivec4 outOffset;//sub-region offset within the output brick image\n"
		"	ivec4 tileOrigin;//source tile origin and size (voxels)\n"
		"	ivec4 tileSize;\n"
		"} pc;\n"
		"\n"
		"//source voxel q, 0 outside the source volume\n"
		"float fetchZero(ivec3 q){\n"
		"	if(any(lessThan(q, ivec3(0))) || any(greaterThanEqual(q, xf.srcDim.xyz))) return 0.0;\n"
		"	return texelFetch(srctile, clamp(q - pc.tileOrigin.xyz, ivec3(0), pc.tileSize.xyz - 1), 0).r;\n"
		"}\n"
		"void main(){\n"
		"	ivec3 gid = ivec3(gl_GlobalInvocationID.xyz);\n"
		"	if(any(greaterThanEqual(gid, pc.validDims.xyz))) return;\n"
		"	vec3 u = vec3(pc.brickOrigin.xyz + gid) * xf.gscale.xyz + xf.goff.xyz;\n"
		"	vec3 p = (xf.G * vec4(u, 1.0)).xyz;\n"
		"	//compensated (Kahan) sum of the radial-basis terms; precise keeps\n"
		"	//the compiler from contracting or reassociating it\n"
		"	precise vec3 sum = vec3(0.0);\n"
		"	precise vec3 comp = vec3(0.0);\n"
		"	int n = xf.cfg.x;\n"
		"	for(int i=0;i<n;i++){\n"
		"		vec3 d = u - lm[2*i].xyz; float r2 = dot(d,d);\n"
		"		float basis = r2>1e-12 ? 0.5*r2*log(r2) : 0.0;\n"
		"		precise vec3 term = lm[2*i+1].xyz * basis - comp;\n"
		"		precise vec3 next = sum + term;\n"
		"		comp = (next - sum) - term;\n"
		"		sum = next;\n"
		"	}\n"
		"	p += sum;\n"
		"	//p: source voxel coords (centers at integers). Beyond one voxel\n"
		"	//outside the volume all taps are 0; testing in float first also\n"
		"	//keeps the int conversions below in range\n"
		"	float v = 0.0;\n"
		"	if(all(greaterThanEqual(p, vec3(-1.0))) && all(lessThanEqual(p, vec3(xf.srcDim.xyz)))){\n"
		"		if(xf.cfg.y == 0){\n"
		"			v = fetchZero(ivec3(floor(p + 0.5)));\n"
		"		} else {\n"
		"			vec3 fp = floor(p);\n"
		"			ivec3 q = ivec3(fp);\n"
		"			vec3 t = p - fp;\n"
		"			for(int dz=0; dz<=1; ++dz)\n"
		"			for(int dy=0; dy<=1; ++dy)\n"
		"			for(int dx=0; dx<=1; ++dx){\n"
		"				float wt = (dx==1 ? t.x : 1.0-t.x) * (dy==1 ? t.y : 1.0-t.y) * (dz==1 ? t.z : 1.0-t.z);\n"
		"				if(wt > 0.0) v += wt * fetchZero(q + ivec3(dx,dy,dz));\n"
		"			}\n"
		"		}\n"
		"	}\n"
		"	imageStore(outimg, gid + pc.outOffset.xyz, vec4(v));\n"
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
		if (program_->create(device_))
		{
			//GLSL compile failure: do not keep a program with a null module
			delete program_;
			program_ = 0;
			return true;
		}
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
