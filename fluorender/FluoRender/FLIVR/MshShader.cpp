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
#include <iostream>
#include <FLIVR/MshShader.h>
#include <FLIVR/ShaderProgram.h>
#include <FLIVR/VolShaderCode.h>

using std::string;
using std::vector;
using std::ostringstream;

namespace FLIVR
{
#define VTX_SHADER_CODE_CORE_PROFILE \
	"//VTX_SHADER_CODE_CORE_PROFILE\n" \
	"layout (binding = 0) uniform VolVertShaderUBO {" \
	"	mat4 matrix0; //projection matrix\n" \
	"	mat4 matrix1; //modelview matrix\n" \
	"} vubo;" \
	"layout(location = 0) in vec3 InVertex;  //w will be set to 1.0 automatically\n" \
	"layout(location = 1) in vec3 InTexture;\n" \
	"layout(location = 0) out vec3 OutVertex;\n" \
	"layout(location = 1) out vec3 OutTexture;\n" \
	"//-------------------\n" \
	"void main()\n" \
	"{\n" \
	"	gl_Position = vubo.matrix0 * vubo.matrix1 * vec4(InVertex,1.);\n" \
	"	OutTexture = InTexture;\n" \
	"	OutVertex  = InVertex;\n" \
	"}\n" 
#define MSH_VERTEX_INPUTS_V(i) \
	"//MSH_VERTEX_INPUTS_V\n" \
	"layout(location = " #i ") in vec4 InVertex;\n"

#define MSH_VERTEX_INPUTS_N(i) \
	"//MSH_VERTEX_INPUTS_N\n" \
	"layout(location = " #i ") in vec4 InNormal;\n"

#define MSH_VERTEX_INPUTS_T(i) \
	"//MSH_VERTEX_INPUTS_T\n" \
	"layout(location = " #i ") in vec2 InTexcoord;\n"

#define MSH_VERTEX_OUTPUTS_N(i) \
	"//MSH_VERTEX_OUTPUTS_N\n" \
	"layout(location = " #i ") out vec3 OutNormal;\n"

#define MSH_VERTEX_OUTPUTS_T(i) \
	"//MSH_VERTEX_OUTPUTS_T\n" \
	"layout(location = " #i ") out vec2 OutTexcoord;\n"

#define MSH_VERTEX_OUTPUTS_FOG(i) \
	"//MSH_VERTEX_OUTPUTS_FOG\n" \
	"layout(location = " #i ") out vec4 OutFogCoord;\n"

#define MSH_VERTEX_OUTPUTS_POS(i) \
	"//MSH_VERTEX_OUTPUTS_POS\n" \
	"layout(location = " #i ") out vec4 OutPosition;\n"

#define MSH_VERTEX_UNIFORM_MATRIX \
	"//MSH_VERTEX_UNIFORM_MATRIX\n" \
	"layout (binding = 0) uniform MshVertShaderUBO {" \
	"	mat4 matrix0; //projection matrix\n" \
	"	mat4 matrix1; //modelview matrix\n" \
	"	mat4 matrix2; //normal\n" \
    "   float threshold; //threshold\n" \
	"} vubo;" \

#define MSH_VERTEX_UNIFORMS_TEX \
    "// MSH_VERTEX_UNIFORMS_TEX\n" \
    "layout (binding = 2) uniform sampler2D tex0;\n"

#define MSH_HEAD \
	"// MSH_HEAD\n" \
	"void main()\n" \
	"{\n"

#define MSH_VERTEX_BODY_POS \
	"//MSH_VERTEX_BODY_POS\n" \
	"	gl_Position = vubo.matrix0 * vubo.matrix1 * vec4(InVertex.xyz, 1.0);\n" \
	"	OutPosition = vec4(InVertex.xyz, 1.0);\n"

#define MSH_VERTEX_BODY_POS_PARTICLE \
    "//MSH_VERTEX_BODY_POS_PARTICLE\n" \
    "    gl_Position = InVertex.w > vubo.threshold ? vubo.matrix0 * vubo.matrix1 * vec4(InVertex.xyz, 1.0) : vec4(0.0);\n" \
    "    OutPosition = vec4(InVertex.xyz, 1.0);\n"

#define MSH_VERTEX_BODY_POS_PALETTE \
    "//MSH_VERTEX_BODY_POS_PALETTE\n" \
    "    gl_Position = texture(tex0, InTexcoord).w >= 0.001 ? vubo.matrix0 * vubo.matrix1 * vec4(InVertex.xyz, 1.0) : vec4(0.0);\n" \
    "    OutPosition = vec4(InVertex.xyz, 1.0);\n"

#define MSH_VERTEX_BODY_NORMAL \
	"//MSH_VERTEX_BODY_NORMAL\n" \
	"	OutNormal = normalize((vubo.matrix2 * vec4(InNormal.xyz, 0.0)).xyz);\n"

#define MSH_VERTEX_BODY_TEX \
	"//MSH_VERTEX_BODY_TEX\n" \
	"	OutTexcoord = InTexcoord;\n"

#define MSH_VERTEX_BODY_FOG \
	"//MSH_VERTEX_BODY_FOG\n" \
	"	OutFogCoord = vubo.matrix1 * vec4(InVertex.xyz,1.);\n"

#define MSH_FRAG_OUTPUTS \
	"//MSH_FRAG_OUTPUTS\n" \
	"layout(location = 0) out vec4 FragColor;\n" \
	"\n"

#define MSH_FRAG_OUTPUTS_INT \
	"//MSH_FRAG_OUTPUTS_INT\n" \
	"layout(location = 0) out uint FragUint;\n"\
	"\n"

#define MSH_FRAG_INPUTS_N(i) \
	"//MSH_FRAG_INPUTS_N\n" \
	"layout(location = " #i ") in vec3 OutNormal;\n"

#define MSH_FRAG_INPUTS_T(i) \
	"//MSH_FRAG_INPUTS_T\n" \
	"layout(location = " #i ") in vec2 OutTexcoord;\n"

#define MSH_FRAG_INPUTS_FOG(i) \
	"//MSH_FRAG_INPUTS_FOG\n" \
	"layout(location = " #i ") in vec4 OutFogCoord;\n"

#define MSH_FRAG_INPUTS_POS(i) \
	"//MSH_FRAG_INPUTS_POS\n" \
	"layout(location = " #i ") in vec4 OutPosition;\n"

#define MSH_FRAG_UNIFORMS \
	"//MSH_FRAG_UNIFORMS_COLOR\n" \
	"layout (binding = 1) uniform MshVertShaderUBO {" \
	"	vec4 loc0;//ambient color\n" \
	"	vec4 loc1;//diffuse color\n" \
	"	vec4 loc2;//specular color\n" \
	"	vec4 loc3;//(shine, alpha, 0, 0)\n" \
	"	vec4 loc7;//(1/vx, 1/vy, 0, 0)\n" \
	"	vec4 loc8;//(int, start, end, 0.0)\n" \
	"	vec4 loc9;//(fog_r, fog_g, fog_b, 0.0)\n" \
	"	vec4 loc10; //plane0\n" \
	"	vec4 loc11; //plane1\n" \
	"	vec4 loc12; //plane2\n" \
	"	vec4 loc13; //plane3\n" \
	"	vec4 loc14; //plane4\n" \
	"	vec4 loc15; //plane5\n" \
	"	mat4 matrix3;\n" \
	"	uint loci0;//name\n" \
	"} fubo;\n" \

#define MSH_FRAG_UNIFORMS_TEX \
	"// MSH_FRAG_UNIFORMS_TEX\n" \
	"layout (binding = 2) uniform sampler2D tex0;\n"

#define MSH_FRAG_UNIFORMS_DP \
	"// MSH_FRAG_UNIFORMS_DP\n" \
	"layout (binding = 3) uniform sampler2D tex1; //tex15 \n"

#define MSH_FRAG_UNIFORMS_SEL_TEX \
    "// MSH_FRAG_UNIFORMS_SEL_TEX\n" \
    "layout (binding = 4) uniform sampler2D tex2;\n"

#define MSH_HEAD_CLIP \
	"	//VOL_HEAD_CLIP\n" \
	"	vec4 clip = fubo.matrix3*OutPosition;\n" \
	"	if (dot(clip.xyz, fubo.loc10.xyz)+fubo.loc10.w < 0.0 ||\n" \
	"		dot(clip.xyz, fubo.loc11.xyz)+fubo.loc11.w < 0.0 ||\n" \
	"		dot(clip.xyz, fubo.loc12.xyz)+fubo.loc12.w < 0.0 ||\n" \
	"		dot(clip.xyz, fubo.loc13.xyz)+fubo.loc13.w < 0.0 ||\n" \
	"		dot(clip.xyz, fubo.loc14.xyz)+fubo.loc14.w < 0.0 ||\n" \
	"		dot(clip.xyz, fubo.loc15.xyz)+fubo.loc15.w < 0.0)\n" \
	"	{\n" \
	"		discard;//FragColor = vec4(0.0);\n" \
	"		return;\n" \
	"	}\n" \
	"\n"
    
#define MSH_HEAD_FOG \
"    //MSH_HEAD_FOG\n" \
"    vec4 fp;\n" \
"    fp.x = fubo.loc8.x;\n" \
"    fp.y = fubo.loc8.y;\n" \
"    fp.z = fubo.loc8.z;\n" \
"    fp.w = abs(OutFogCoord.z/OutFogCoord.w);\n" \
"\n"

//1: draw depth after 15 (15)
#define MSH_FRAG_BODY_DP_1 \
	"	// MSH_FRAG_BODY_DP_1\n" \
	"	vec2 t = vec2(gl_FragCoord.x*fubo.loc7.x, gl_FragCoord.y*fubo.loc7.y);\n" \
	"	if (texture(tex1, t).r >= gl_FragCoord.z-1e-6) discard;\n"

//2: draw mesh after 14 (14, 15)
#define MSH_FRAG_BODY_DP_2 \
	"	// MSH_FRAG_BODY_DP_2\n" \
	"	vec2 t = vec2(gl_FragCoord.x*fubo.loc7.x, gl_FragCoord.y*fubo.loc7.y);\n" \
	"	if (texture(tex14, t).r >= gl_FragCoord.z-1e-6) discard;\n" \

//3: draw mesh after 13 and before 15 (13, 14, 15)
#define MSH_FRAG_BODY_DP_3 \
	"	// MSH_FRAG_BODY_DP_3\n" \
	"	vec2 t = vec2(gl_FragCoord.x*fubo.loc7.x, gl_FragCoord.y*fubo.loc7.y);\n" \
	"	if (texture(tex1, t).r <= gl_FragCoord.z+1e-6) discard;\n" \
	"	else if (texture(tex13, t).r >= gl_FragCoord.z-1e-6) discard;\n"

//4: draw mesh before 15 (at 14) (14, 15)
#define MSH_FRAG_BODY_DP_4 \
	"	// MSH_FRAG_BODY_DP_4\n" \
	"	vec2 t = vec2(gl_FragCoord.x*fubo.loc7.x, gl_FragCoord.y*fubo.loc7.y);\n" \
	"	if (texture(tex1, t).r <= gl_FragCoord.z+1e-6) discard;\n" \

//5: draw mesh at 15 (15)
#define MSH_FRAG_BODY_DP_5 \
	"	// MSH_FRAG_BODY_DP_5\n" \
	"	vec2 t = vec2(gl_FragCoord.x*fubo.loc7.x, gl_FragCoord.y*fubo.loc7.y);\n" \
	"	if (texture(tex1, t).r <= gl_FragCoord.z-1e-6) discard;\n" \

#define MSH_FRAG_BODY_COLOR \
	"	//MSH_FRAG_BODY_COLOR\n" \
	"	vec4 c = vec4(1.0);\n"

#define MSH_FRAG_BODY_COLOR_OUT \
	"	// MSH_FRAG_BODY_COLOR_OUT\n" \
	"	FragColor = vec4(c.xyz, c.w*fubo.loc3.y);\n"

#define MSH_FRAG_BODY_SIMPLE \
	"	//MSH_FRAG_BODY_SIMPLE\n" \
	"	vec4 c = fubo.loc0;\n"

#define MSH_FRAG_BODY_COLOR_LIGHT \
	"	//MSH_FRAG_BODY_COLOR_LIGHT\n" \
	"	vec4 spec = vec4(0.0);\n" \
	"	vec3 eye = vec3(0.0, 0.0, 1.0);\n" \
	"	vec3 l_dir = vec3(0.0, 0.0, 1.0);\n" \
	"	vec3 n = normalize(OutNormal);\n" \
	"	float intensity = abs(dot(n, l_dir));\n" \
	"	if (intensity > 0.0)\n" \
	"	{\n" \
	"		vec3 h = normalize(l_dir+eye);\n" \
	"		float intSpec = max(dot(h, n), 0.0);\n" \
	"		spec = fubo.loc2 * pow(intSpec, fubo.loc3.x);\n" \
	"	}\n" \
	"	c.xyz = max(intensity * fubo.loc1 + spec, fubo.loc0).xyz;\n"

#define MSH_FRAG_BODY_TEXTURE \
	"	//MSH_FRAG_BODY_TEXTURE\n" \
	"	c = c * texture(tex0, OutTexcoord);\n"

#define MSH_FRAG_BODY_PALETTE_SIMPLE \
    "    //MSH_FRAG_BODY_PALETTE_SIMPLE\n" \
    "    vec4 c = texture(tex0, OutTexcoord);\n"

#define MSH_FRAG_BODY_PALETTE_LIGHT \
    "    //MSH_FRAG_BODY_PALETTE_LIGHT\n" \
    "    vec4 c = texture(tex0, OutTexcoord);\n" \
    "    vec4 spec = vec4(0.0);\n" \
    "    vec3 eye = vec3(0.0, 0.0, 1.0);\n" \
    "    vec3 l_dir = vec3(0.0, 0.0, 1.0);\n" \
    "    vec3 n = normalize(OutNormal);\n" \
    "    float intensity = abs(dot(n, l_dir));\n" \
    "    if (intensity > 0.0)\n" \
    "    {\n" \
    "        vec3 h = normalize(l_dir+eye);\n" \
    "        float intSpec = max(dot(h, n), 0.0);\n" \
    "        spec = c * pow(intSpec, fubo.loc3.x);\n" \
    "    }\n" \
    "    c.xyz = max(intensity * c + spec, c * 0.5).xyz;\n"

#define MSH_FRAG_BODY_SELECTION_PALETTE_SIMPLE \
    "    //MSH_FRAG_BODY_SELECTION_PALETTE_SIMPLE\n" \
    "    if (texture(tex0, OutTexcoord).w < 0.001) discard;\n" \
    "    vec4 c = texture(tex2, OutTexcoord);\n"

#define MSH_FRAG_BODY_SELECTION_PALETTE_LIGHT \
    "    //MSH_FRAG_BODY_SELECTION_PALETTE_LIGHT\n" \
    "    if (texture(tex0, OutTexcoord).w < 0.001) discard;\n" \
    "    vec4 c = texture(tex2, OutTexcoord);\n" \
    "    vec4 spec = vec4(0.0);\n" \
    "    vec3 eye = vec3(0.0, 0.0, 1.0);\n" \
    "    vec3 l_dir = vec3(0.0, 0.0, 1.0);\n" \
    "    vec3 n = normalize(OutNormal);\n" \
    "    float intensity = abs(dot(n, l_dir));\n" \
    "    if (intensity > 0.0)\n" \
    "    {\n" \
    "        vec3 h = normalize(l_dir+eye);\n" \
    "        float intSpec = max(dot(h, n), 0.0);\n" \
    "        spec = c * pow(intSpec, fubo.loc3.x);\n" \
    "    }\n" \
    "    c.xyz = max(intensity * c + spec, c * 0.5).xyz;\n"

#define MSH_FRAG_BODY_FOG \
	"	// MSH_FRAG_BODY_FOG\n" \
	"	vec4 v;\n" \
    "   v.x = (fp.z-fp.w)/(fp.z-fp.y);\n" \
    "   v.x = 1.0-clamp(v.x, 0.0, 1.0);\n" \
    "   v.x = 1.0-exp(-pow(v.x*2.5, 2.0));\n" \
    "   c.xyz = mix(c.xyz, vec3(fubo.loc9.x, fubo.loc9.y, fubo.loc9.z), v.x*fp.x); \n" \
    "\n"

#define MSH_FRAG_BODY_INT \
	"	//MSH_FRAG_BODY_INT\n" \
	"	FragUint = fubo.loci0;\n"

#define MSH_FRAG_BODY_INT_SEGMENT \
    "    //MSH_FRAG_BODY_INT_SEGMENT\n" \
    "    FragUint = uint(OutTexcoord.x * 256.0) + uint(OutTexcoord.y * 256.0) * 256;\n"

#define MSH_TAIL \
	"}\n"

	MshShader::MshShader(VkDevice device, 
		int type,
		int peel, bool tex,
		bool fog, bool light, bool particle)
		: device_(device),
		type_(type),
		peel_(peel),
		tex_(tex),
		fog_(fog),
		light_(light),
        particle_(particle),
		program_(0)
	{}

	MshShader::~MshShader()
	{
		delete program_;
	}

	bool MshShader::create()
	{
		string vs;
		if (emit_v(vs)) return true;
		string fs;
		if (emit_f(fs)) return true;
		program_ = new ShaderProgram(vs, fs);
		program_->create(device_);
		return false;
	}

	bool MshShader::emit_v(string& s)
	{
		ostringstream z;

		z << ShaderProgram::glsl_version_;

		//inputs
		z << MSH_VERTEX_INPUTS_V(0);
		z << MSH_VERTEX_OUTPUTS_POS(0);
		if (type_ == 0 || type_ == 3)
		{
			if (light_ && tex_)
			{
				z << MSH_VERTEX_INPUTS_N(1);
				z << MSH_VERTEX_INPUTS_T(2);
			}
			else if (light_)
				z << MSH_VERTEX_INPUTS_N(1);
			else if (tex_)
				z << MSH_VERTEX_INPUTS_T(2);
			
			if (light_ && tex_)
			{
				z << MSH_VERTEX_OUTPUTS_N(1);
				z << MSH_VERTEX_OUTPUTS_T(2);
				if (fog_) z << MSH_VERTEX_OUTPUTS_FOG(3);
			}
			else if (light_)
			{
				z << MSH_VERTEX_OUTPUTS_N(1);
				if (fog_) z << MSH_VERTEX_OUTPUTS_FOG(2);
			}
			else if (tex_)
			{
				z << MSH_VERTEX_OUTPUTS_T(1);
				if (fog_) z << MSH_VERTEX_OUTPUTS_FOG(2);
			}
			else if (fog_) z << MSH_VERTEX_OUTPUTS_FOG(1);

			//uniforms
			z << MSH_VERTEX_UNIFORM_MATRIX;
		}
		else if (type_ == 1)
        {
            if (tex_)
                z << MSH_VERTEX_INPUTS_T(2);
			z << MSH_VERTEX_UNIFORM_MATRIX;
        }
        else if (type_ == 2)
        {
            z << MSH_VERTEX_INPUTS_N(1);
            z << MSH_VERTEX_INPUTS_T(2);
            z << MSH_VERTEX_OUTPUTS_T(1);
            z << MSH_VERTEX_UNIFORM_MATRIX;
            z << MSH_VERTEX_UNIFORMS_TEX;
        }
        
        if (tex_)
            z << MSH_VERTEX_UNIFORMS_TEX;

		z << MSH_HEAD;

		//body
        if (particle_)
            z << MSH_VERTEX_BODY_POS_PARTICLE;
        else if (tex_)
            z << MSH_VERTEX_BODY_POS_PALETTE;
        else
            z << MSH_VERTEX_BODY_POS;
        
		if (type_ == 0 || type_ == 3)
		{
			if (light_)
				z << MSH_VERTEX_BODY_NORMAL;
			if (tex_)
				z << MSH_VERTEX_BODY_TEX;
			if (fog_)
				z << MSH_VERTEX_BODY_FOG;
		}
        else if (type_ == 2)
            z << MSH_VERTEX_BODY_TEX;

		z << MSH_TAIL;

		s = z.str();
        
        std::cerr << s << std::endl;
        
		return false;
	}

	bool MshShader::emit_f(string& s)
	{
		ostringstream z;

		z << ShaderProgram::glsl_version_;

		if (type_ == 0 || type_ == 3)
		{
			z << MSH_FRAG_OUTPUTS;
			//inputs
			z << MSH_FRAG_INPUTS_POS(0);
			z << MSH_FRAG_UNIFORMS;
			if (light_ && tex_)
			{
				z << MSH_FRAG_INPUTS_N(1);
				z << MSH_FRAG_INPUTS_T(2);
				if (fog_) z << MSH_FRAG_INPUTS_FOG(3);
			}
			else if (light_)
			{
				z << MSH_FRAG_INPUTS_N(1);
				if (fog_) z << MSH_FRAG_INPUTS_FOG(2);
			}
			else if (tex_)
			{
				z << MSH_FRAG_INPUTS_T(1);
				if (fog_) z << MSH_FRAG_INPUTS_FOG(2);
			}
			else if (fog_) z << MSH_FRAG_INPUTS_FOG(1);

			//uniforms
			if (tex_)
				z << MSH_FRAG_UNIFORMS_TEX;
			if (peel_)
				z << MSH_FRAG_UNIFORMS_DP;
            if (type_ == 3)
                z << MSH_FRAG_UNIFORMS_SEL_TEX;

			z << MSH_HEAD;
			z << MSH_HEAD_CLIP;

			//body
			switch (peel_)
			{
			case 1:
				z << MSH_FRAG_BODY_DP_1;
				break;
			case 2:
				z << MSH_FRAG_BODY_DP_2;
				break;
			case 3:
				z << MSH_FRAG_BODY_DP_3;
				break;
			case 4:
				z << MSH_FRAG_BODY_DP_4;
				break;
			case 5:
				z << MSH_FRAG_BODY_DP_5;
				break;
			}

			if (fog_)
				z << MSH_HEAD_FOG;
            if (light_)
            {
                if (tex_)
                {
                    if (type_ == 3)
                        z << MSH_FRAG_BODY_SELECTION_PALETTE_LIGHT;
                    else
                        z << MSH_FRAG_BODY_PALETTE_LIGHT;
                }
                else
                {
                    z << MSH_FRAG_BODY_COLOR;
                    z << MSH_FRAG_BODY_COLOR_LIGHT;
                }
            }
            else
            {
                if (tex_)
                {
                    if (type_ == 3)
                        z << MSH_FRAG_BODY_SELECTION_PALETTE_SIMPLE;
                    else
                        z << MSH_FRAG_BODY_PALETTE_SIMPLE;
                }
                else
                    z << MSH_FRAG_BODY_SIMPLE;
            }
			if (fog_)
			{
				z << MSH_FRAG_BODY_FOG;
			}
			z << MSH_FRAG_BODY_COLOR_OUT;
		}
		else if (type_ == 1)
		{
			z << MSH_FRAG_OUTPUTS_INT;
			z << MSH_FRAG_INPUTS_POS(0);
			z << MSH_FRAG_UNIFORMS;
			z << MSH_HEAD;
			z << MSH_HEAD_CLIP;
			z << MSH_FRAG_BODY_INT;
		}
        else if (type_ == 2)
        {
            z << MSH_FRAG_OUTPUTS_INT;
            z << MSH_FRAG_INPUTS_POS(0);
            z << MSH_FRAG_INPUTS_T(1);
            z << MSH_FRAG_UNIFORMS;
            z << MSH_HEAD;
            z << MSH_HEAD_CLIP;
            z << MSH_FRAG_BODY_INT_SEGMENT;
        }

		z << MSH_TAIL;

		s = z.str();
        
        //std::cerr << s << std::endl;

		return false;
	}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	MshShaderFactory::MshShaderFactory()
		: prev_shader_(-1)
	{}

	MshShaderFactory::MshShaderFactory(std::vector<vks::VulkanDevice*>& devices)
		: prev_shader_(-1)
	{
		init(devices);
	}

	void MshShaderFactory::init(std::vector<vks::VulkanDevice*>& devices)
	{
		vdevices_ = devices;
		setupDescriptorSetLayout();
	}

	MshShaderFactory::~MshShaderFactory()
	{
		for(unsigned int i=0; i<shader_.size(); i++)
			delete shader_[i];

		for (auto vdev : vdevices_)
		{
			VkDevice device = vdev->logicalDevice;

			vkDestroyPipelineLayout(device, pipeline_[vdev].pipelineLayout, nullptr);
			vkDestroyDescriptorSetLayout(device, pipeline_[vdev].descriptorSetLayout, nullptr);

		}
	}

	ShaderProgram* MshShaderFactory::shader(VkDevice device,
		int type,
		int peel, bool tex,
		bool fog, bool light, bool particle)
	{
		if(prev_shader_ >= 0)
		{
			if(shader_[prev_shader_]->match(device, type, peel, tex, fog, light, particle))
			{
				return shader_[prev_shader_]->program();
			}
		}
		for(unsigned int i=0; i<shader_.size(); i++)
		{
			if(shader_[i]->match(device, type, peel, tex, fog, light, particle))
			{
				prev_shader_ = i;
				return shader_[i]->program();
			}
		}

		MshShader* s = new MshShader(device, type, peel, tex, fog, light, particle);
		if(s->create())
		{
			delete s;
			return 0;
		}
		shader_.push_back(s);
		prev_shader_ = (int)shader_.size()-1;
		return s->program();
	}

	void MshShaderFactory::setupDescriptorSetLayout()
	{
		for (auto vdev : vdevices_)
		{
			VkDevice device = vdev->logicalDevice;

			std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings =
			{
				// Binding 0 : Uniform buffer for vertex shader
				vks::initializers::descriptorSetLayoutBinding(
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				VK_SHADER_STAGE_VERTEX_BIT,
				0),
				// Binding 1 : Base uniform buffer for fragment shader
				vks::initializers::descriptorSetLayoutBinding(
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				VK_SHADER_STAGE_FRAGMENT_BIT,
				1),
			};

			int offset = 2;
			for (int i = 0; i < MshShader::MSH_SAMPLER_NUM; i++)
			{
				setLayoutBindings.push_back(
					vks::initializers::descriptorSetLayoutBinding(
						VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
						VK_SHADER_STAGE_FRAGMENT_BIT|VK_SHADER_STAGE_VERTEX_BIT,
						offset + i)
				);
			}

			VkDescriptorSetLayoutCreateInfo descriptorLayout =
				vks::initializers::descriptorSetLayoutCreateInfo(
					setLayoutBindings.data(),
					static_cast<uint32_t>(setLayoutBindings.size()));

			descriptorLayout.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
			descriptorLayout.pNext = nullptr;

			MshPipelineSettings pipe;
			VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &pipe.descriptorSetLayout));

			VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
				vks::initializers::pipelineLayoutCreateInfo(
					&pipe.descriptorSetLayout,
					1);

			VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &pipe.pipelineLayout));

			pipeline_[vdev] = pipe;
		}
	}

	void MshShaderFactory::getDescriptorSetWriteUniforms(vks::VulkanDevice* vdev, MshUniformBufs& uniformBuffers, std::vector<VkWriteDescriptorSet>& writeDescriptorSets)
	{
		VkDevice device = vdev->logicalDevice;

		writeDescriptorSets.push_back(
			vks::initializers::writeDescriptorSet(
				VK_NULL_HANDLE,
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				0,
				&uniformBuffers.vert.descriptor)
		);
		writeDescriptorSets.push_back(
			vks::initializers::writeDescriptorSet(
				VK_NULL_HANDLE,
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				1,
				&uniformBuffers.frag.descriptor)
		);
	}

	void MshShaderFactory::getDescriptorSetWriteUniforms(vks::VulkanDevice* vdev, vks::Buffer& vert, vks::Buffer& frag, std::vector<VkWriteDescriptorSet>& writeDescriptorSets)
	{
		VkDevice device = vdev->logicalDevice;

		writeDescriptorSets.push_back(
			vks::initializers::writeDescriptorSet(
				VK_NULL_HANDLE,
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				0,
				&vert.descriptor)
		);
		writeDescriptorSets.push_back(
			vks::initializers::writeDescriptorSet(
				VK_NULL_HANDLE,
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				1,
				&frag.descriptor)
		);
	}

	// Prepare and initialize uniform buffer containing shader uniforms
	void MshShaderFactory::prepareUniformBuffers(std::map<vks::VulkanDevice*, MshUniformBufs>& uniformBuffers)
	{
		for (auto vulkanDev : vdevices_)
		{
			MshUniformBufs uniformbufs;

			VK_CHECK_RESULT(vulkanDev->createBuffer(
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
				&uniformbufs.vert,
				sizeof(MshVertShaderUBO)));

			VK_CHECK_RESULT(vulkanDev->createBuffer(
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
				&uniformbufs.frag,
				sizeof(MshFragShaderUBO)));

			// Map persistent
			VK_CHECK_RESULT(uniformbufs.vert.map());
			VK_CHECK_RESULT(uniformbufs.frag.map());

			uniformBuffers[vulkanDev] = uniformbufs;
		}
	}

	void MshShaderFactory::updateUniformBuffers(MshUniformBufs& uniformBuffers, MshVertShaderUBO vubo, MshFragShaderUBO fubo)
	{
		memcpy(uniformBuffers.vert.mapped, &vubo, sizeof(vubo));
		memcpy(uniformBuffers.frag.mapped, &fubo, sizeof(fubo));
	}

} // end namespace FLIVR
