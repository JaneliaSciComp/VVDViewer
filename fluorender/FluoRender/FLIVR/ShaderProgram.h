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

#ifndef ShaderProgram_h 
#define ShaderProgram_h

#include <string>
#include <memory>

#include "vulkan/vulkan.h"

#ifdef _DARWIN
#include "Public/ShaderLang.h"
#include "SPIRV/GlslangToSpv.h"
#else
#include "glslang/SPIRV/GlslangToSpv.h"
#endif

#include "DLLExport.h"

namespace FLIVR
{

	class EXPORT_API ShaderProgram
	{
	public:
		ShaderProgram(const std::string& vert_shader,const std::string& frag_shader);
		ShaderProgram(const std::string& compute_shader);
		~ShaderProgram();

		bool create(VkDevice device);
		bool valid();
		void destroy();

		VkPipelineShaderStageCreateInfo get_vertex_shader() { return vert_shader_stage_; }
		VkPipelineShaderStageCreateInfo get_fragment_shader() { return frag_shader_stage_; }
		VkPipelineShaderStageCreateInfo get_compute_shader() { return compute_shader_stage_; }
		std::string get_vertex_shader_code() { return vert_shader_; }
		std::string get_fragment_shader_code() { return frag_shader_; }
		std::string get_compute_shader_code() { return compute_shader_; }

		static const int MAX_SHADER_UNIFORMS = 16;
		static std::string glsl_version_;

		static EShLanguage FindLanguage(const VkShaderStageFlagBits shader_type);
		static void init_resources(TBuiltInResource &Resources);
		static bool GLSLtoSPV(const VkShaderStageFlagBits shader_type, const char *pshader, std::vector<unsigned int> &spirv);
		static void init_glslang();
		static void finalize_glslang();

	protected:
		std::string  vert_shader_;
		std::string  frag_shader_;
		std::string  compute_shader_;
		VkPipelineShaderStageCreateInfo vert_shader_stage_;
		VkPipelineShaderStageCreateInfo frag_shader_stage_;
		VkPipelineShaderStageCreateInfo compute_shader_stage_;

		//validation
		bool valid_;

		VkDevice device_;

	};

} // end namespace FLIVR

#endif // ShaderProgram_h
