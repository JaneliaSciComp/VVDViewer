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

#include "ShaderProgram.h"
#include "Vulkan/vulkanexamplebase.h"
#include "Utils.h"
#include "../compatibility.h"
#include <time.h>
#include <cstdio>
#include <sstream>
#include <iostream>
#include <cfloat>
#include <mutex>

using std::string;

namespace FLIVR
{
	string ShaderProgram::glsl_version_ = "#version 450\n";

	ShaderProgram::ShaderProgram(const string& compute_shader) :
	vert_shader_(""), frag_shader_(""), compute_shader_(compute_shader), valid_(false)
	{
		device_ = VK_NULL_HANDLE;
		vert_shader_stage_.module = VK_NULL_HANDLE;
		frag_shader_stage_.module = VK_NULL_HANDLE;
		compute_shader_stage_.module = VK_NULL_HANDLE;
	}
	ShaderProgram::ShaderProgram(const string& vert_shader, const string& frag_shader) :
	vert_shader_(vert_shader), frag_shader_(frag_shader), compute_shader_(""), valid_(false)
	{
		device_ = VK_NULL_HANDLE;
		vert_shader_stage_.module = VK_NULL_HANDLE;
		frag_shader_stage_.module = VK_NULL_HANDLE;
		compute_shader_stage_.module = VK_NULL_HANDLE;
	}

	ShaderProgram::~ShaderProgram()
	{
		destroy();
	}

	bool ShaderProgram::valid()
	{
		return valid_;
	}

	bool ShaderProgram::create(VkDevice device)
	{

		init_glslang();
		VkShaderModuleCreateInfo moduleCreateInfo;

		if (!vert_shader_.empty()) {
			std::vector<unsigned int> vtx_spv;
			vert_shader_stage_.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			vert_shader_stage_.pNext = NULL;
			vert_shader_stage_.pSpecializationInfo = NULL;
			vert_shader_stage_.flags = 0;
			vert_shader_stage_.stage = VK_SHADER_STAGE_VERTEX_BIT;
			vert_shader_stage_.pName = "main";

			if ( !GLSLtoSPV(VK_SHADER_STAGE_VERTEX_BIT, vert_shader_.data(), vtx_spv) )
				return true;

			moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			moduleCreateInfo.pNext = NULL;
			moduleCreateInfo.flags = 0;
			moduleCreateInfo.codeSize = vtx_spv.size() * sizeof(unsigned int);
			moduleCreateInfo.pCode = vtx_spv.data();
			VK_CHECK_RESULT( vkCreateShaderModule(device, &moduleCreateInfo, NULL, &vert_shader_stage_.module) );
		}

		if (!frag_shader_.empty()) {
			std::vector<unsigned int> frag_spv;
			frag_shader_stage_.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			frag_shader_stage_.pNext = NULL;
			frag_shader_stage_.pSpecializationInfo = NULL;
			frag_shader_stage_.flags = 0;
			frag_shader_stage_.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			frag_shader_stage_.pName = "main";

			if ( !GLSLtoSPV(VK_SHADER_STAGE_FRAGMENT_BIT, frag_shader_.data(), frag_spv) )
				return true;

			moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			moduleCreateInfo.pNext = NULL;
			moduleCreateInfo.flags = 0;
			moduleCreateInfo.codeSize = frag_spv.size() * sizeof(unsigned int);
			moduleCreateInfo.pCode = frag_spv.data();
			VK_CHECK_RESULT( vkCreateShaderModule(device, &moduleCreateInfo, NULL, &frag_shader_stage_.module) );
		}

		if (!compute_shader_.empty()) {
			std::vector<unsigned int> compute_spv;
			compute_shader_stage_.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			compute_shader_stage_.pNext = NULL;
			compute_shader_stage_.pSpecializationInfo = NULL;
			compute_shader_stage_.flags = 0;
			compute_shader_stage_.stage = VK_SHADER_STAGE_COMPUTE_BIT;
			compute_shader_stage_.pName = "main";

			if (!GLSLtoSPV(VK_SHADER_STAGE_COMPUTE_BIT, compute_shader_.data(), compute_spv))
				return true;

			moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			moduleCreateInfo.pNext = NULL;
			moduleCreateInfo.flags = 0;
			moduleCreateInfo.codeSize = compute_spv.size() * sizeof(unsigned int);
			moduleCreateInfo.pCode = compute_spv.data();
			VK_CHECK_RESULT(vkCreateShaderModule(device, &moduleCreateInfo, NULL, &compute_shader_stage_.module));
		}

		device_ = device;

		valid_ = true;

		return false;

	}

	void ShaderProgram::destroy()
	{
		if (device_ != VK_NULL_HANDLE)
		{
			if (vert_shader_stage_.module != VK_NULL_HANDLE) 
				vkDestroyShaderModule(device_, vert_shader_stage_.module, NULL);
			if (frag_shader_stage_.module != VK_NULL_HANDLE) 
				vkDestroyShaderModule(device_, frag_shader_stage_.module, NULL);
			if (compute_shader_stage_.module != VK_NULL_HANDLE)
				vkDestroyShaderModule(device_, compute_shader_stage_.module, NULL);
		}
	}

	EShLanguage ShaderProgram::FindLanguage(const VkShaderStageFlagBits shader_type) {
		switch (shader_type) {
		case VK_SHADER_STAGE_VERTEX_BIT:
			return EShLangVertex;

		case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:
			return EShLangTessControl;

		case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:
			return EShLangTessEvaluation;

		case VK_SHADER_STAGE_GEOMETRY_BIT:
			return EShLangGeometry;

		case VK_SHADER_STAGE_FRAGMENT_BIT:
			return EShLangFragment;

		case VK_SHADER_STAGE_COMPUTE_BIT:
			return EShLangCompute;

		default:
			return EShLangVertex;
		}
	}

	void ShaderProgram::init_glslang() {
		//glslang process initialization rebuilds the built-in symbol tables, which is
		//expensive; initialize once for the process lifetime instead of per compile
		static std::once_flag once;
		std::call_once(once, []() { glslang::InitializeProcess(); });
	}

	void ShaderProgram::finalize_glslang() {
		glslang::FinalizeProcess();
	}

	void ShaderProgram::init_resources(TBuiltInResource &Resources) {
		Resources.maxLights = 32;
		Resources.maxClipPlanes = 6;
		Resources.maxTextureUnits = 32;
		Resources.maxTextureCoords = 32;
		Resources.maxVertexAttribs = 64;
		Resources.maxVertexUniformComponents = 4096;
		Resources.maxVaryingFloats = 64;
		Resources.maxVertexTextureImageUnits = 32;
		Resources.maxCombinedTextureImageUnits = 80;
		Resources.maxTextureImageUnits = 32;
		Resources.maxFragmentUniformComponents = 4096;
		Resources.maxDrawBuffers = 32;
		Resources.maxVertexUniformVectors = 128;
		Resources.maxVaryingVectors = 8;
		Resources.maxFragmentUniformVectors = 16;
		Resources.maxVertexOutputVectors = 16;
		Resources.maxFragmentInputVectors = 15;
		Resources.minProgramTexelOffset = -8;
		Resources.maxProgramTexelOffset = 7;
		Resources.maxClipDistances = 8;
		Resources.maxComputeWorkGroupCountX = 65535;
		Resources.maxComputeWorkGroupCountY = 65535;
		Resources.maxComputeWorkGroupCountZ = 65535;
		Resources.maxComputeWorkGroupSizeX = 1024;
		Resources.maxComputeWorkGroupSizeY = 1024;
		Resources.maxComputeWorkGroupSizeZ = 64;
		Resources.maxComputeUniformComponents = 1024;
		Resources.maxComputeTextureImageUnits = 16;
		Resources.maxComputeImageUniforms = 8;
		Resources.maxComputeAtomicCounters = 8;
		Resources.maxComputeAtomicCounterBuffers = 1;
		Resources.maxVaryingComponents = 60;
		Resources.maxVertexOutputComponents = 64;
		Resources.maxGeometryInputComponents = 64;
		Resources.maxGeometryOutputComponents = 128;
		Resources.maxFragmentInputComponents = 128;
		Resources.maxImageUnits = 8;
		Resources.maxCombinedImageUnitsAndFragmentOutputs = 8;
		Resources.maxCombinedShaderOutputResources = 8;
		Resources.maxImageSamples = 0;
		Resources.maxVertexImageUniforms = 0;
		Resources.maxTessControlImageUniforms = 0;
		Resources.maxTessEvaluationImageUniforms = 0;
		Resources.maxGeometryImageUniforms = 0;
		Resources.maxFragmentImageUniforms = 8;
		Resources.maxCombinedImageUniforms = 8;
		Resources.maxGeometryTextureImageUnits = 16;
		Resources.maxGeometryOutputVertices = 256;
		Resources.maxGeometryTotalOutputComponents = 1024;
		Resources.maxGeometryUniformComponents = 1024;
		Resources.maxGeometryVaryingComponents = 64;
		Resources.maxTessControlInputComponents = 128;
		Resources.maxTessControlOutputComponents = 128;
		Resources.maxTessControlTextureImageUnits = 16;
		Resources.maxTessControlUniformComponents = 1024;
		Resources.maxTessControlTotalOutputComponents = 4096;
		Resources.maxTessEvaluationInputComponents = 128;
		Resources.maxTessEvaluationOutputComponents = 128;
		Resources.maxTessEvaluationTextureImageUnits = 16;
		Resources.maxTessEvaluationUniformComponents = 1024;
		Resources.maxTessPatchComponents = 120;
		Resources.maxPatchVertices = 32;
		Resources.maxTessGenLevel = 64;
		Resources.maxViewports = 16;
		Resources.maxVertexAtomicCounters = 0;
		Resources.maxTessControlAtomicCounters = 0;
		Resources.maxTessEvaluationAtomicCounters = 0;
		Resources.maxGeometryAtomicCounters = 0;
		Resources.maxFragmentAtomicCounters = 8;
		Resources.maxCombinedAtomicCounters = 8;
		Resources.maxAtomicCounterBindings = 1;
		Resources.maxVertexAtomicCounterBuffers = 0;
		Resources.maxTessControlAtomicCounterBuffers = 0;
		Resources.maxTessEvaluationAtomicCounterBuffers = 0;
		Resources.maxGeometryAtomicCounterBuffers = 0;
		Resources.maxFragmentAtomicCounterBuffers = 1;
		Resources.maxCombinedAtomicCounterBuffers = 1;
		Resources.maxAtomicCounterBufferSize = 16384;
		Resources.maxTransformFeedbackBuffers = 4;
		Resources.maxTransformFeedbackInterleavedComponents = 64;
		Resources.maxCullDistances = 8;
		Resources.maxCombinedClipAndCullDistances = 8;
		Resources.maxSamples = 4;
		Resources.maxMeshOutputVerticesNV = 256;
		Resources.maxMeshOutputPrimitivesNV = 512;
		Resources.maxMeshWorkGroupSizeX_NV = 32;
		Resources.maxMeshWorkGroupSizeY_NV = 1;
		Resources.maxMeshWorkGroupSizeZ_NV = 1;
		Resources.maxTaskWorkGroupSizeX_NV = 32;
		Resources.maxTaskWorkGroupSizeY_NV = 1;
		Resources.maxTaskWorkGroupSizeZ_NV = 1;
		Resources.maxMeshViewCountNV = 4;
		Resources.limits.nonInductiveForLoops = 1;
		Resources.limits.whileLoops = 1;
		Resources.limits.doWhileLoops = 1;
		Resources.limits.generalUniformIndexing = 1;
		Resources.limits.generalAttributeMatrixVectorIndexing = 1;
		Resources.limits.generalVaryingIndexing = 1;
		Resources.limits.generalSamplerIndexing = 1;
		Resources.limits.generalVariableIndexing = 1;
		Resources.limits.generalConstantMatrixVectorIndexing = 1;
	}

	//
	// Compile a given string containing GLSL into SPV for use by VK
	// Return value of false means an error was encountered.
	//
	bool ShaderProgram::GLSLtoSPV(const VkShaderStageFlagBits shader_type, const char *pshader, std::vector<unsigned int> &spirv) {
		EShLanguage stage = FindLanguage(shader_type);
		glslang::TShader shader(stage);
		glslang::TProgram program;
		const char *shaderStrings[1];
		TBuiltInResource Resources = {};
		init_resources(Resources);

		// Enable SPIR-V and Vulkan rules when parsing GLSL
		EShMessages messages = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);

		shaderStrings[0] = pshader;
		shader.setStrings(shaderStrings, 1);

		if (!shader.parse(&Resources, 100, false, messages)) {
			puts(shader.getInfoLog());
			puts(shader.getInfoDebugLog());
            puts(pshader);
			//auto aa = shader.getInfoLog();
			//auto bb = shader.getInfoDebugLog();
			return false;  // something didn't work
		}

		program.addShader(&shader);

		//
		// Program-level processing...
		//

		if (!program.link(messages)) {
			puts(shader.getInfoLog());
			puts(shader.getInfoDebugLog());
            puts(pshader);
			fflush(stdout);
			return false;
		}

		glslang::GlslangToSpv(*program.getIntermediate(stage), spirv);
		return true;
	}



} // end namespace FLIVR
