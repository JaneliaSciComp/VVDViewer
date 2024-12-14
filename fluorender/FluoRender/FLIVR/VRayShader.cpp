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
#include <regex>
#include <FLIVR/VRayShader.h>
#include <FLIVR/ShaderProgram.h>

using std::string;
using std::vector;
using std::ostringstream;

namespace FLIVR
{
#define CORE_PROFILE_VTX_SHADER 1

#define VRAY_VTX_SHADER_CODE_CORE_PROFILE \
	"//VTX_SHADER_CODE_CORE_PROFILE\n" \
	"layout (binding = 0) uniform VolVertShaderUBO {\n" \
	"	mat4 matrix0; //projection matrix\n" \
	"	mat4 matrix1; //modelview matrix\n" \
	"} vubo;\n" \
	"// VOL_UNIFORMS_BRICK\n" \
	"layout(push_constant) uniform VolFragShaderBrickConst {\n" \
	"	vec4 brkscale;//tex transform for bricking and 1/nx\n" \
	"	vec4 brktrans;//tex transform for bricking and 1/ny\n" \
	"	vec4 mskbrkscale;//tex transform for mask bricks and 1/nz\n" \
	"	vec4 mskbrktrans;//tex transform for mask bricks\n" \
	"	vec4 tbmin;//tex bbox min\n" \
	"	vec4 tbmax;//tex bbox max\n" \
	"	vec3 loc4;//(zmin, zmax, dz)\n" \
	"	uint stnum;\n" \
	"} brk;\n" \
	"\n" \
	"layout(location = 0) in vec3 InVertex;  //w will be set to 1.0 automatically\n" \
	"layout(location = 1) in vec3 InTexture;\n" \
	"layout(location = 0) out vec4 OutVertex;\n" \
	"layout(location = 1) out vec3 OutTexture;\n" \
	"//-------------------\n" \
	"void main()\n" \
	"{\n" \
	"	gl_Position = vubo.matrix0 * vubo.matrix1 * vec4(InVertex*brk.brkscale.xyz + brk.brktrans.xyz, 1.0);\n" \
	"	OutTexture = InTexture;\n" \
	"	OutVertex  = gl_Position;\n" \
	"}\n" 

#define VRAY_FRG_HEADER \
	"#pragma optionNV(inline all)\n" \
	"#pragma optionNV(fastmath on)\n" \
	"#pragma optionNV(ifcvt none)\n" \
	"#pragma optionNV(strict on)\n" \
	"#pragma optionNV(unroll all)\n" \
	"layout(location = 0) in vec4 OutVertex;\n" \
	"layout(location = 1) in vec3 OutTexture;\n" \
	"layout(location = 0) out vec4 FragColor;\n" \
	"\n" \
	"layout(binding = 1) uniform VolFragShaderBaseUBO {\n" \
	"	vec4 loc0;//(lx, ly, lz, alpha)\n" \
	"	vec4 loc1;//(ka, kd, ks, ns)\n" \
	"	vec4 loc2;//(scalar_scale, gm_scale, left_thresh, right_thresh)\n" \
	"	vec4 loc3;//(gamma, gm_thresh, offset, sw)\n" \
	"	vec4 loc5;//(spcx, spcy, spcz, max_id)\n" \
	"	vec4 loc6;//(r, g, b, 0.0) or (1/vx, 1/vy, luminance, depth_mode)\n" \
	"	vec4 loc7;//(1/vx, 1/vy, 1/sample_rate, highlight_thresh)\n" \
	"	vec4 loc8;//(int, start, end, 0.0)\n" \
	"	vec4 loc9;//(fog_r, fog_g, fog_b, 0.0)\n" \
	"	vec4 loc10; //plane0\n" \
	"	vec4 loc11; //plane1\n" \
	"	vec4 loc12; //plane2\n" \
	"	vec4 loc13; //plane3\n" \
	"	vec4 loc14; //plane4\n" \
	"	vec4 loc15; //plane5\n" \
	"	mat4 matrix0; //inverted projection matrix\n" \
	"	mat4 matrix1; //inverted modelview matrix\n" \
	"} base;\n" \
	"\n" \
	"layout(binding = 2) uniform sampler3D tex0;//data volume\n" \
	"layout(binding = 3) uniform sampler3D tex1;//gm volume\n" \
	"\n" \
	"layout(push_constant) uniform VolFragShaderBrickConst {\n" \
	"	vec4 brkscale;//tex transform for bricking and 1/nx\n" \
	"	vec4 brktrans;//tex transform for bricking and 1/ny\n" \
	"	vec4 mskbrkscale;//tex transform for mask bricks and 1/nz\n" \
	"	vec4 mskbrktrans;//tex transform for mask bricks\n" \
	"	vec4 tbmin;//tex bbox min and mode\n" \
	"	vec4 tbmax;//tex bbox max\n" \
	"	vec3 loc4;//(zmin, zmax, dz)\n" \
	"	uint stnum;\n" \
	"} brk;\n" \
	"\n" 

#define VRAY_UNIFORMS_INDEX_COLOR \
	"//VRAY_UNIFORMS_INDEX_COLOR\n" \
	"layout (binding = 9) uniform sampler2D tex7;\n" \
	"\n"

#define VRAY_UNIFORMS_INDEX_COLOR_D \
    "//VRAY_UNIFORMS_INDEX_COLOR_D\n" \
	"layout (binding = 9) uniform sampler2D tex7;\n" \
    "\n"

#define VRAY_UNIFORMS_DP \
	"//VRAY_UNIFORMS_DP\n" \
	"layout (binding = 16) uniform sampler2D tex14;//depth texture 1\n" \
	"layout (binding = 17) uniform sampler2D tex15;//depth texture 2\n" \
	"\n"

#define VRAY_UNIFORMS_MASK \
	"//VRAY_UNIFORMS_MASK\n" \
	"layout (binding = 4) uniform sampler3D tex2;//3d mask volume\n" \
	"\n"

#define VRAY_UNIFORMS_LABEL \
	"//VRAY_UNIFORMS_LABEL\n" \
	"layout (binding = 5) uniform usampler3D tex3;//3d label volume\n" \
	"\n"

#define VRAY_UNIFORMS_DEPTHMAP \
	"//VRAY_UNIFORMS_DEPTHMAP\n" \
	"layout (binding = 6) uniform sampler2D tex4;//2d depth map\n" \
	"\n"

#define VRAY_CLIP_FUNC \
	"//VRAY_CLIP_FUNC\n" \
	"bool vol_clip_func(vec4 t)\n" \
	"{\n" \
	"	vec3 brickt = (t.xyz * brk.brkscale.xyz + brk.brktrans.xyz);\n" \
	"	if (dot(brickt.xyz, base.loc10.xyz) + base.loc10.w < 0.0 ||\n" \
	"		dot(brickt.xyz, base.loc11.xyz) + base.loc11.w < 0.0 ||\n" \
	"		dot(brickt.xyz, base.loc12.xyz) + base.loc12.w < 0.0 ||\n" \
	"		dot(brickt.xyz, base.loc13.xyz) + base.loc13.w < 0.0 ||\n" \
	"		dot(brickt.xyz, base.loc14.xyz) + base.loc14.w < 0.0 ||\n" \
	"		dot(brickt.xyz, base.loc15.xyz) + base.loc15.w < 0.0)\n" \
	"		return true;\n" \
	"	else\n" \
	"		return false;\n" \
	"}\n" \
	"\n"

#define VRAY_HEAD_PERSP \
	"//VRAY_HEAD_PERSP\n" \
	"void main()\n" \
	"{\n" \
	"	const vec3 vray = normalize(base.matrix0 * OutVertex).xyz;\n" \
	"	const vec4 ray = base.matrix1 * vec4(vray, 0.0) / vec4(brk.brkscale.xyz, 1.);\n" \
	"	const vec4 st = base.matrix1 * vec4((brk.loc4.x / vray.z) * vray, 1.0) / vec4(brk.brkscale.xyz, 1.) - vec4(brk.brktrans.xyz / brk.brkscale.xyz, 0.0);\n" \
	"	const float step = brk.loc4.z / vray.z;\n" \
	"	vec4 outcol = vec4(0.0);\n" \
	"	vec4 dir = vec4(brk.brkscale.w, brk.brktrans.w, brk.mskbrkscale.w, 0.0);\n" \
	"\n"
	

#define VRAY_HEAD_ORTHO \
	"//VRAY_HEAD_ORTHO\n" \
	"void main()\n" \
	"{\n" \
	"	const vec4 ray = base.matrix1 * vec4(0.0, 0.0, 1.0, 0.0) / vec4(brk.brkscale.xyz, 1.);\n" \
	"	const vec4 st = base.matrix1 * vec4((base.matrix0 * OutVertex).xy, brk.loc4.x, 1.0) / vec4(brk.brkscale.xyz, 1.) - vec4(brk.brktrans.xyz / brk.brkscale.xyz, 0.0);\n" \
	"	const float step = brk.loc4.z;\n" \
	"	vec4 outcol = vec4(0.0);\n" \
	"	vec4 dir = vec4(brk.brkscale.w, brk.brktrans.w, brk.mskbrkscale.w, 0.0);\n" \
	"\n"

#define VRAY_HEAD_PERSP_DMAP \
	"//VRAY_HEAD_PERSP_DMAP\n" \
	"void main()\n" \
	"{\n" \
	"	const vec3 vray = normalize(base.matrix0 * OutVertex).xyz;\n" \
	"	const vec4 ray = base.matrix1 * vec4(vray, 0.0) / vec4(brk.brkscale.xyz, 1.);\n" \
	"	const vec4 st = base.matrix1 * vec4((brk.loc4.y / vray.z) * vray, 1.0) / vec4(brk.brkscale.xyz, 1.) - vec4(brk.brktrans.xyz / brk.brkscale.xyz, 0.0);\n" \
	"	const float step = -brk.loc4.z / vray.z;\n" \
	"	vec4 outcol = vec4(0.0);\n" \
	"	vec4 dir = vec4(brk.brkscale.w, brk.brktrans.w, brk.mskbrkscale.w, 0.0);\n" \
	"	const vec4 dmap_ray = vec4(vray, 0.0);\n" \
	"	const vec4 dmap_st = vec4((brk.loc4.y / vray.z) * vray, 1.0);\n" \
	"	const mat4 proj_mat = inverse(base.matrix0);\n" \
	"	float prevz = 1.0;\n" \
	"\n"

#define VRAY_HEAD_ORTHO_DMAP \
	"//VRAY_HEAD_ORTHO_DMAP\n" \
	"void main()\n" \
	"{\n" \
	"	const vec4 ray = base.matrix1 * vec4(0.0, 0.0, 1.0, 0.0) / vec4(brk.brkscale.xyz, 1.);\n" \
	"	const vec4 st = base.matrix1 * vec4((base.matrix0 * OutVertex).xy, brk.loc4.y, 1.0) / vec4(brk.brkscale.xyz, 1.) - vec4(brk.brktrans.xyz / brk.brkscale.xyz, 0.0);\n" \
	"	const float step = -brk.loc4.z;\n" \
	"	vec4 outcol = vec4(0.0);\n" \
	"	vec4 dir = vec4(brk.brkscale.w, brk.brktrans.w, brk.mskbrkscale.w, 0.0);\n" \
	"	const vec4 dmap_ray = vec4(0.0, 0.0, 1.0, 0.0);\n" \
	"	const vec4 dmap_st = vec4((base.matrix0 * OutVertex).xy, brk.loc4.y, 1.0);\n" \
	"	const mat4 proj_mat = inverse(base.matrix0);\n" \
	"	float prevz = 1.0;\n" \
	"\n"

#define VRAY_HEAD_CLIP_COORD_PERSP \
	"	//VRAY_CLIP_COORD_PERSP\n" \
	"	const vec4 dmap_ray = vec4(vray, 0.0);\n" \
	"	const vec4 dmap_st = vec4((brk.loc4.x / vray.z) * vray, 1.0);\n" \
	"	const mat4 proj_mat = inverse(base.matrix0);\n" \
	"\n"
#define VRAY_HEAD_CLIP_COORD_ORTHO \
	"	//VRAY_CLIP_COORD_PERSP\n" \
	"	const vec4 dmap_ray = vec4(0.0, 0.0, 1.0, 0.0);\n" \
	"	const vec4 dmap_st = vec4((base.matrix0 * OutVertex).xy, brk.loc4.x, 1.0);\n" \
	"	const mat4 proj_mat = inverse(base.matrix0);\n" \
	"\n"

#define VRAY_MASK_RAY_PERSP \
	"//VRAY_MASK_RAY_PERSP\n" \
	"	const vec4 msk_ray = base.matrix1 * vec4(vray, 0.0) / vec4(brk.mskbrkscale.xyz, 1.);\n" \
	"	const vec4 msk_st = base.matrix1 * vec4((brk.loc4.x / vray.z) * vray, 1.0) / vec4(brk.mskbrkscale.xyz, 1.) - vec4(brk.mskbrktrans.xyz / brk.mskbrkscale.xyz, 0.0);\n" \
	"\n"

#define VRAY_MASK_RAY_ORTHO \
	"//VRAY_MASK_RAY_ORTHO\n" \
	"	const vec4 msk_ray = base.matrix1 * vec4(0.0, 0.0, 1.0, 0.0) / vec4(brk.brkscale.xyz, 1.);\n" \
	"	const vec4 msk_st = base.matrix1 * vec4((base.matrix0 * OutVertex).xy, brk.loc4.x, 1.0) / vec4(brk.mskbrkscale.xyz, 1.) - vec4(brk.mskbrktrans.xyz / brk.mskbrkscale.xyz, 0.0);\n" \
	"\n"

#define VRAY_HEAD_LIT \
	"	//VRAY_HEAD_LIT\n" \
	"	vec4 l = base.loc0; // {lx, ly, lz, alpha}\n" \
	"	vec4 k = base.loc1; // {ka, kd, ks, ns}\n" \
	"	k.x = k.x > 1.0 ? log2(3.0 - k.x) : k.x;\n" \
	"	vec4 n, w;\n" \
	"	if (l.w == 0.0) { discard; return; }\n" \
	"\n" 

#define VRAY_HEAD_FOG \
	"	//VRAY_HEAD_FOG\n" \
	"	vec4 fp;\n" \
	"	fp.x = base.loc8.x;\n" \
	"	fp.y = base.loc8.y;\n" \
	"	fp.z = base.loc8.z;\n" \
	"	const mat4 mv_mat = inverse(base.matrix1);\n" \
	"\n"

#define VRAY_HEAD_ID_INITIALIZE \
	"	//VRAY_HEAD_ID_INITIALIZE\n" \
	"	vec4 prev_pos = st - ray*step;\n" \
	"	uint prev_id = uint(texture(tex0, prev_pos.stp).x*base.loc5.w+0.5);\n" \
	"	prev_id = !vol_clip_func(prev_pos) ? prev_id : 0;\n" \
	"	bool inside = false;\n" \
	"\n"

#define VRAY_HEAD_MIP \
	"	//VRAY_HEAD_MIP\n" \
	"	float maxval = 0.0;\n" \
	"\n"

#define VRAY_HEAD_MULTI \
	"	//VRAY_HEAD_MULTI\n" \
	"	int mode = uint(tbmin.w);\n" \
	"\n"

#define VRAY_HEAD_LABEL_SEG \
	"	//VRAY_HEAD_LABEL_SEG\n" \
	"	float highlight = 0.0;\n" \
	"\n"

#define VRAY_HEAD_HIGHLIGHT \
    "    //VRAY_HEAD_HIGHLIGHT\n" \
    "    bool highlight = false;\n" \
    "    float maxplusmin = max(max(base.loc6.r, base.loc6.g), base.loc6.b) + min(min(base.loc6.r, base.loc6.g), base.loc6.b);\n" \
    "    vec4 hcol = vec4(maxplusmin - base.loc6.r, maxplusmin - base.loc6.g, maxplusmin - base.loc6.b, 0.2);\n" \
    "\n"

#define VRAY_LOOP_HEAD \
	"	//VRAY_LOOP_HEAD\n" \
	"	for (uint i = 0; i <= brk.stnum; i++)\n" \
	"	{\n" \
	"		vec4 TexCoord = st + ray*step*float(i);\n" \
	"		vec4 t = vec4(TexCoord.xyz, 1.0);\n" \
	"\n"

#define VRAY_LOOP_CLIP_COORD \
	"		//VRAY_LOOP_CLIP_COORD\n" \
	"		vec4 cspv = proj_mat * (dmap_st + dmap_ray*step*float(i));\n" \
	"		cspv = cspv / cspv.w;\n" \
	"\n"

#define VRAY_DP_Z1_FAR \
	"		//VRAY_DP_Z_FAR\n" \
	"		float dp_far = texture(tex15, vec2((cspv.x + 1.0)/2.0, (cspv.y + 1.0)/2.0)).r;\n" \
	"\n"
#define VRAY_DP_Z2_NEAR \
	"		//VRAY_DP_Z_NEAR\n" \
	"		float dp_near = texture(tex14, vec2((cspv.x + 1.0)/2.0, (cspv.y + 1.0)/2.0)).r;\n" \
	"\n"

#define VRAY_MASK_VAL\
	"		//VRAY_MASK_VAL\n" \
	"		float maskval = texture(tex2, (msk_st + msk_ray*step*float(i)).stp).r; //get mask value\n" \

#define VRAY_LOOP_CONDITION_START \
	"		if ("

#define VRAY_LOOP_CONDITION_DP_1 \
	"cspv.z < dp_far && " 

#define VRAY_LOOP_CONDITION_DP_2 \
	"cspv.z > dp_near && " 

#define VRAY_LOOP_CONDITION_DP_3 \
	"cspv.z > dp_near && cspv.z < dp_far && "

#define VRAY_LOOP_CONDITION_HIDE_INSIDE_MASK \
	"maskval <= 0.5 && "

#define VRAY_LOOP_CONDITION_HIDE_OUTSIDE_MASK \
	"maskval > 0.5 && "

#define VRAY_LOOP_CONDITION_END \
	"!vol_clip_func(t) && t.x >= brk.tbmin.x && t.x <= brk.tbmax.x && t.y >= brk.tbmin.y && t.y <= brk.tbmax.y && t.z >= brk.tbmin.z && t.z <= brk.tbmax.z && outcol.w <= 0.99995)\n" \
	"		{\n" 
#define VRAY_LOOP_CONDITION_END_DMAP \
	"!vol_clip_func(t) && t.x >= brk.tbmin.x && t.x <= brk.tbmax.x && t.y >= brk.tbmin.y && t.y <= brk.tbmax.y && t.z >= brk.tbmin.z && t.z <= brk.tbmax.z)\n" \
	"		{\n" 

#define VRAY_HIGHLIGHT_THRESHOLD \
    "           //VRAY_HIGHLIGHT_THRESHOLD\n" \
    "           highlight = texture(tex0, t.stp).r >= base.loc7.w || highlight ? true : false;\n" \
    "\n"

#define VRAY_MULTI_DEFAULT_COLORMAP_INDEX_BODY \
	"			//VRAY_MULTI_DEFAULT_COLORMAP_INDEX_BODY\n" \
	"			if (mode & 8 != 0) \n" \
	"			{\n" \
	"				//VRAY_INDEX_COLOR_BODY_SHADE\n" \
	"				vec4 v;\n" \
	"				uint id = uint(texture(tex0, t.stp).x*base.loc5.w+0.5);\n" \
	"				vec4 c = vec4(0.0);\n" \
	"				if (!inside) \n" \
	"				{\n" \
	"					prev_pos = vec4((t - ray*step).xyz, 1.0);\n" \
	"					prev_id = uint(texture(tex0, prev_pos.xyz).x * base.loc5.w + 0.5);\n" \
	"				}\n" \
	"				vec4 post_pos = vec4((t + ray*step).xyz, 1.0);\n" \
	"				uint post_id = uint(texture(tex0, post_pos.xyz).x * base.loc5.w + 0.5);\n" \
	"				if ( (inside && (id != prev_id || id != post_id || vol_clip_func(post_pos))) || (!inside && (vol_clip_func(prev_pos) || id != prev_id || id != post_id)) )" \
	"				{\n" \
	"					c = texture(tex7, vec2((float(id%uint(256))+0.5)/256.0, (float(id/256)+0.5)/256.0));\n" \
	"\n" \
	"					if (mode & 1 != 0) \n" \
	"					{\n" \
	"						vec4 p; \n" \
	"						uint r; \n" \
	"						v = vec4(0.0); \n" \
	"						n = vec4(0.0); \n" \
	"						w = vec4(0.0);\n" \
	"						w.x = dir.x; \n" \
	"						p = clamp(TexCoord + w, 0.0, 1.0); \n" \
	"						r = !vol_clip_func(p) ? uint(texture(tex0, p.stp).x*base.loc5.w+0.5) : 0; \n" \
	"						v.x = (id==r?0.5:0.0) ; \n" \
	"						n.x = v.x + n.x; \n" \
	"						p = clamp(TexCoord - w, 0.0, 1.0); \n" \
	"						r = !vol_clip_func(p) ? uint(texture(tex0, p.stp).x*base.loc5.w+0.5) : 0; \n" \
	"						v.x = (id==r?0.5:0.0) ; \n" \
	"						n.x = v.x - n.x; \n" \
	"						w = vec4(0.0); \n" \
	"						w.y = dir.y; \n" \
	"						p = clamp(TexCoord + w, 0.0, 1.0); \n" \
	"						r = !vol_clip_func(p) ? uint(texture(tex0, p.stp).x*base.loc5.w+0.5) : 0; \n" \
	"						v.x = (id==r?0.5:0.0) ; \n" \
	"						n.y = v.x + n.y; \n" \
	"						p = clamp(TexCoord - w, 0.0, 1.0); \n" \
	"						r = !vol_clip_func(p) ? uint(texture(tex0, p.stp).x*base.loc5.w+0.5) : 0; \n" \
	"						v.x = (id==r?0.5:0.0) ; \n" \
	"						n.y = v.x - n.y; \n" \
	"						w = vec4(0.0); \n" \
	"						w.z = dir.z; \n" \
	"						p = clamp(TexCoord + w, 0.0, 1.0); \n" \
	"						r = !vol_clip_func(p) ? uint(texture(tex0, p.stp).x*base.loc5.w+0.5) : 0; \n" \
	"						v.x = (id==r?0.5:0.0) ; \n" \
	"						n.z = v.x + n.z; \n" \
	"						p = clamp(TexCoord - w, 0.0, 1.0); \n" \
	"						r = !vol_clip_func(p) ? uint(texture(tex0, p.stp).x*base.loc5.w+0.5) : 0; \n" \
	"						v.x = (id==r?0.5:0.0) ; \n" \
	"						n.z = v.x - n.z; \n" \
	"						p.y = length(n.xyz); \n" \
	"						p.y = 0.5 * (base.loc2.x<0.0?(1.0+p.y*base.loc2.x):p.y*base.loc2.x); \n" \
	"\n" \
	"						//VOL_BODY_SHADING\n" \
	"						n.xyz = normalize(n.xyz);\n" \
	"						n.w = dot(l.xyz, n.xyz); // calculate angle between light and normal. \n" \
	"						n.w = clamp(abs(n.w), 0.0, 1.0); // two-sided lighting, n.w = abs(cos(angle))  \n" \
	"						w = k; // w.x = weight*ka, w.y = weight*kd, w.z = weight*ks \n" \
	"						w.x = k.x - w.y; // w.x = ka - kd*weight \n" \
	"						w.x = w.x + k.y; // w.x = ka + kd - kd*weight \n" \
	"						n.z = pow(n.w, k.w); // n.z = abs(cos(angle))^ns \n" \
	"						n.w = (n.w * w.y) + w.x; // n.w = abs(cos(angle))*kd+ka\n" \
	"						n.z = w.z * n.z; // n.z = weight*ks*abs(cos(angle))^ns \n" \
	"\n" \
	"						//VOL_COMPUTED_GM_LOOKUP\n" \
	"						v.y = p.y;\n" \
	"\n" \
	"						//VOL_COLOR_OUTPUT\n" \
	"						c.xyz = c.xyz*clamp(1.0-base.loc1.x, 0.0, 1.0) + base.loc1.x*c.xyz*(base.loc1.y > 0.0?(n.w + n.z):1.0);\n" \
	"						c.xyz *= pow(1.0 - base.loc1.x/2.0, 2.0) + 1.0;\n" \
	"\n" \
	"					}\n" \
	"\n" \
	"					c.rgb = c.rgb*base.loc6.z;\n" \
	"					c = c * l.w;\n" \
	"				}\n" \
	"				prev_id = id;\n" \
	"				inside = true;\n" \
	"\n" \
	"				c = (mode & 2 == 0) ? c*l.w : c;\n" \
	"\n" \
	"				//VRAY_BLEND_OVER\n" \
	"				outcol = outcol + (1.0 - outcol.w) * c;\n" \
	"\n" \
	"			}\n" \
	"			else \n" \
	"			{\n" \
	"				//VRAY_DATA_VOLUME_LOOKUP\n" \
	"				vec4 v = texture(tex0, t.stp);\n" \
	"\n"\
	"				// VOL_GRAD_COMPUTE_HI\n" \
	"				vec4 r, p;\n" \
	"				v = vec4(v.x);\n" \
	"				n = vec4(0.0);\n" \
	"				w = vec4(0.0);\n" \
	"				w.x = dir.x;\n" \
	"				p = clamp(TexCoord + w, 0.0, 1.0);\n" \
	"				r = texture(tex0, p.stp);\n" \
	"				n.x = r.x + n.x;\n" \
	"				p = clamp(TexCoord - w, 0.0, 1.0);\n" \
	"				r = texture(tex0, p.stp);\n" \
	"				n.x = r.x - n.x;\n" \
	"				w = vec4(0.0);\n" \
	"				w.y = dir.y;\n" \
	"				p = clamp(TexCoord + w, 0.0, 1.0);\n" \
	"				r = texture(tex0, p.stp);\n" \
	"				n.y = r.x + n.y;\n" \
	"				p = clamp(TexCoord - w, 0.0, 1.0);\n" \
	"				r = texture(tex0, p.stp);\n" \
	"				n.y = r.x - n.y;\n" \
	"				w = vec4(0.0);\n" \
	"				w.z = dir.z;\n" \
	"				p = clamp(TexCoord + w, 0.0, 1.0);\n" \
	"				r = texture(tex0, p.stp);\n" \
	"				n.z = r.x + n.z;\n" \
	"				p = clamp(TexCoord - w, 0.0, 1.0);\n" \
	"				r = texture(tex0, p.stp);\n" \
	"				n.z = r.x - n.z;\n" \
	"				p.y = length(n.xyz);\n" \
	"				p.y = 0.5 * (base.loc2.x < 0.0 ? (1.0 + p.y * base.loc2.x) : p.y * base.loc2.x);\n" \
	"\n" \
	"				if (mode & 1 != 0) \n" \
	"				{\n" \
	"					//VRAY_BODY_SHADING\n" \
	"					n.xyz = normalize(n.xyz);\n" \
	"					n.w = dot(l.xyz, n.xyz); // calculate angle between light and normal. \n" \
	"					n.w = clamp(abs(n.w), 0.0, 1.0); // two-sided lighting, n.w = abs(cos(angle))  \n" \
	"					w = k; // w.x = weight*ka, w.y = weight*kd, w.z = weight*ks \n" \
	"					w.x = k.x - w.y; // w.x = ka - kd*weight \n" \
	"					w.x = w.x + k.y; // w.x = ka + kd - kd*weight \n" \
	"					n.z = pow(n.w, k.w); // n.z = abs(cos(angle))^ns \n" \
	"					n.w = (n.w * w.y) + w.x; // n.w = abs(cos(angle))*kd+ka\n" \
	"					n.z = w.z * n.z; // n.z = weight*ks*abs(cos(angle))^ns \n" \
	"				}\n" \
	"\n" \
	"				//VRAY_COMPUTED_GM_LOOKUP\n" \
	"				v.y = p.y;\n" \
	"\n" \
	"				if (mode & 4 != 0) \n" \
	"				{\n" \
	"					//VRAY_TRANSFER_FUNCTION_COLORMAP\n" \
	"					vec4 c;\n" \
	"					float tf_alp = 0.0;\n" \
	"					float alpha = 0.0;\n" \
	"					v.x = base.loc2.x<0.0?(1.0+v.x*base.loc2.x):v.x*base.loc2.x;\n" \
	"					if (v.x<base.loc2.z-base.loc3.w || v.x>base.loc2.w+base.loc3.w || v.y<base.loc3.y)\n" \
	"						c = vec4(0.0);\n" \
	"					else\n" \
	"					{\n" \
	"						v.x = (v.x<base.loc2.z?(base.loc3.w-base.loc2.z+v.x)/base.loc3.w:(v.x>base.loc2.w?(base.loc3.w-v.x+base.loc2.w)/base.loc3.w:1.0))*v.x;\n" \
	"						vec4 rb = vec4(0.0);\n" \
	"						//VRAY_TRANSFER_FUNCTION_COLORMAP_VALU \n" \
	"						float valu = (v.x - base.loc6.x) / base.loc6.z;\n" \
	"						//VRAY_COLORMAP_CALC0 \n" \
	"						rb.r = clamp(4.0 * valu - 2.0, 0.0, 1.0);\n" \
	"						rb.g = clamp(valu < 0.5 ? 4.0 * valu : -4.0 * valu + 4.0, 0.0, 1.0);\n" \
	"						rb.b = clamp(-4.0 * valu + 2.0, 0.0, 1.0);\n" \
	"						//VRAY_COMMON_TRANSFER_FUNCTION_CALC \n" \
	"						tf_alp = pow(clamp(v.x / base.loc3.z, \n" \
	"							base.loc3.x < 1.0 ? -(base.loc3.x - 1.0) * 0.00001 : 0.0, \n" \
	"							base.loc3.x>1.0 ? 0.9999 : 1.0), base.loc3.x); \n" \
	"						//VRAY_TRANSFER_FUNCTION_COLORMAP_RESULT \n" \
	"						float alpha = (1.0 - pow(clamp(1.0 - tf_alp * l.w, 0.0, 1.0), base.loc7.z)) / l.w; \n" \
	"						c = vec4(rb.rgb * tf_alp, 1.0); \n" \
	"						c = (mode & 2 != 0) ? c : c*alpha;\n" \
	"					}\n" \
	"					c.w = (mode & 2 != 0) ? 1.0 : c.w;\n" \
	"				}\n" \
	"				else \n" \
	"				{\n" \
	"					//VRAY_TRANSFER_FUNCTION_SIN_COLOR\n" \
	"					vec4 c;\n" \
	"					float tf_alp = 0.0;\n" \
	"					float alpha = 0.0;\n" \
	"					v.x = base.loc2.x < 0.0 ? (1.0 + v.x * base.loc2.x) : v.x * base.loc2.x;\n" \
	"					if (v.x < base.loc2.z - base.loc3.w || v.x > base.loc2.w + base.loc3.w || v.y < base.loc3.y - base.loc3.w)\n" \
	"						c = vec4(0.0);\n" \
	"					else\n" \
	"					{\n" \
	"						v.x = (v.x < base.loc2.z ? (base.loc3.w - base.loc2.z + v.x) / base.loc3.w : (v.x > base.loc2.w ? (base.loc3.w - v.x + base.loc2.w) / base.loc3.w : 1.0)) * v.x;\n" \
	"						v.x = (v.y < base.loc3.y ? (base.loc3.w - base.loc3.y + v.y) / base.loc3.w : 1.0) * v.x;\n" \
	"						tf_alp = pow(clamp(v.x / base.loc3.z,\n" \
	"							base.loc3.x < 1.0 ? -(base.loc3.x - 1.0) * 0.00001 : 0.0,\n" \
	"							base.loc3.x > 1.0 ? 0.9999 : 1.0), base.loc3.x);\n" \
	"						alpha = (1.0 - pow(clamp(1.0 - tf_alp * l.w, 0.0, 1.0), base.loc7.z)) / l.w;\n" \
	"						c = vec4(base.loc6.rgb * tf_alp, 1.0);\n" \
	"						c = (mode & 2 != 0) ? c : c*alpha;\n" \
	"					}\n" \
	"					c.w = (mode & 2 != 0) ? 1.0 : c.w;\n" \
	"				}\n" \
	"\n" \
	"				if (mode & 1 != 0) \n" \
	"				{\n" \
	"					//VRAY_COLOR_OUTPUT\n" \
	"					c.xyz = c.xyz * clamp(1.0 - base.loc1.x, 0.0, 1.0) + base.loc1.x * c.xyz * (base.loc1.y > 0.0 ? (n.w + n.z) : 1.0);\n" \
	"					c.xyz *= pow(1.0 - base.loc1.x / 2.0, 2.0) + 1.0;\n" \
	"				}\n" \
	"\n" \
	"				c = (mode & 2 == 0) ? c*l.w : c;\n" \
	"\n" \
	"				//VRAY_BLEND_OVER\n" \
	"				outcol = outcol + (1.0 - outcol.w) * c;\n" \
	"\n" \
	"			}\n" \
	"\n"

#define VRAY_INDEX_COLOR_BODY \
	"			//VRAY_INDEX_COLOR_BODY\n" \
	"			vec4 v;\n" \
	"			uint id = uint(texture(tex0, t.stp).x*base.loc5.w+0.5);\n" \
	"			vec4 c = vec4(0.0);\n" \
	"			if (!inside) \n" \
	"			{\n" \
	"				prev_pos = vec4((t - ray*step).xyz, 1.0);\n" \
	"				prev_id = uint(texture(tex0, prev_pos.xyz).x * base.loc5.w + 0.5);\n" \
	"			}\n" \
	"			vec4 post_pos = vec4((t + ray*step).xyz, 1.0);\n" \
	"			uint post_id = uint(texture(tex0, post_pos.xyz).x * base.loc5.w + 0.5);\n" \
	"			if ( (inside && (id != prev_id || id != post_id || vol_clip_func(post_pos))) || (!inside && (vol_clip_func(prev_pos) || id != prev_id || id != post_id)) )" \
	"			{\n" \
	"				c = texture(tex7, vec2((float(id%uint(256))+0.5)/256.0, (float(id/256)+0.5)/256.0));\n" \
	"\n" \
	"				//VOL_COLOR_OUTPUT\n" \
	"				c.rgb = c.rgb*base.loc6.z;\n" \
	"				c = c * l.w;\n" \
	"			}\n" \
	"			prev_id = id;\n" \
	"			inside = true;\n" \
	"\n"

#define VRAY_INDEX_COLOR_BODY_SHADE \
	"			//VRAY_INDEX_COLOR_BODY_SHADE\n" \
	"			vec4 v;\n" \
	"			uint id = uint(texture(tex0, t.stp).x*base.loc5.w+0.5);\n" \
	"			vec4 c = vec4(0.0);\n" \
	"			if (!inside) \n" \
	"			{\n" \
	"				prev_pos = vec4((t - ray*step).xyz, 1.0);\n" \
	"				prev_id = uint(texture(tex0, prev_pos.xyz).x * base.loc5.w + 0.5);\n" \
	"			}\n" \
	"			vec4 post_pos = vec4((t + ray*step).xyz, 1.0);\n" \
	"			uint post_id = uint(texture(tex0, post_pos.xyz).x * base.loc5.w + 0.5);\n" \
	"			if ( (inside && (id != prev_id || id != post_id || vol_clip_func(post_pos))) || (!inside && (vol_clip_func(prev_pos) || id != prev_id || id != post_id)) )" \
	"			{\n" \
	"				c = texture(tex7, vec2((float(id%uint(256))+0.5)/256.0, (float(id/256)+0.5)/256.0));\n" \
	"				vec4 p; \n" \
	"				uint r; \n" \
	"				v = vec4(0.0); \n" \
	"				n = vec4(0.0); \n" \
	"				w = vec4(0.0);\n" \
	"				w.x = dir.x; \n" \
	"				p = clamp(TexCoord + w, 0.0, 1.0); \n" \
	"				r = !vol_clip_func(p) ? uint(texture(tex0, p.stp).x*base.loc5.w+0.5) : 0; \n" \
	"				v.x = (id==r?0.5:0.0) ; \n" \
	"				n.x = v.x + n.x; \n" \
	"				p = clamp(TexCoord - w, 0.0, 1.0); \n" \
	"				r = !vol_clip_func(p) ? uint(texture(tex0, p.stp).x*base.loc5.w+0.5) : 0; \n" \
	"				v.x = (id==r?0.5:0.0) ; \n" \
	"				n.x = v.x - n.x; \n" \
	"				w = vec4(0.0); \n" \
	"				w.y = dir.y; \n" \
	"				p = clamp(TexCoord + w, 0.0, 1.0); \n" \
	"				r = !vol_clip_func(p) ? uint(texture(tex0, p.stp).x*base.loc5.w+0.5) : 0; \n" \
	"				v.x = (id==r?0.5:0.0) ; \n" \
	"				n.y = v.x + n.y; \n" \
	"				p = clamp(TexCoord - w, 0.0, 1.0); \n" \
	"				r = !vol_clip_func(p) ? uint(texture(tex0, p.stp).x*base.loc5.w+0.5) : 0; \n" \
	"				v.x = (id==r?0.5:0.0) ; \n" \
	"				n.y = v.x - n.y; \n" \
	"				w = vec4(0.0); \n" \
	"				w.z = dir.z; \n" \
	"				p = clamp(TexCoord + w, 0.0, 1.0); \n" \
	"				r = !vol_clip_func(p) ? uint(texture(tex0, p.stp).x*base.loc5.w+0.5) : 0; \n" \
	"				v.x = (id==r?0.5:0.0) ; \n" \
	"				n.z = v.x + n.z; \n" \
	"				p = clamp(TexCoord - w, 0.0, 1.0); \n" \
	"				r = !vol_clip_func(p) ? uint(texture(tex0, p.stp).x*base.loc5.w+0.5) : 0; \n" \
	"				v.x = (id==r?0.5:0.0) ; \n" \
	"				n.z = v.x - n.z; \n" \
	"				p.y = length(n.xyz); \n" \
	"				p.y = 0.5 * (base.loc2.x<0.0?(1.0+p.y*base.loc2.x):p.y*base.loc2.x); \n" \
	"\n" \
	"				//VOL_BODY_SHADING\n" \
	"				n.xyz = normalize(n.xyz);\n" \
	"				n.w = dot(l.xyz, n.xyz); // calculate angle between light and normal. \n" \
	"				n.w = clamp(abs(n.w), 0.0, 1.0); // two-sided lighting, n.w = abs(cos(angle))  \n" \
	"				w = k; // w.x = weight*ka, w.y = weight*kd, w.z = weight*ks \n" \
	"				w.x = k.x - w.y; // w.x = ka - kd*weight \n" \
	"				w.x = w.x + k.y; // w.x = ka + kd - kd*weight \n" \
	"				n.z = pow(n.w, k.w); // n.z = abs(cos(angle))^ns \n" \
	"				n.w = (n.w * w.y) + w.x; // n.w = abs(cos(angle))*kd+ka\n" \
	"				n.z = w.z * n.z; // n.z = weight*ks*abs(cos(angle))^ns \n" \
	"\n" \
	"				//VOL_COMPUTED_GM_LOOKUP\n" \
	"				v.y = p.y;\n" \
	"\n" \
	"				//VOL_COLOR_OUTPUT\n" \
	"				c.xyz = c.xyz*clamp(1.0-base.loc1.x, 0.0, 1.0) + base.loc1.x*c.xyz*(base.loc1.y > 0.0?(n.w + n.z):1.0);\n" \
	"				c.xyz *= pow(1.0 - base.loc1.x/2.0, 2.0) + 1.0;\n" \
	"				c.rgb = c.rgb*base.loc6.z;\n" \
	"				c = c * l.w;\n" \
	"			}\n" \
	"			prev_id = id;\n" \
	"			inside = true;\n" \
	"\n"

#define VRAY_INDEX_COLOR_D_BODY \
	"			//VRAY_INDEX_COLOR_D_BODY\n" \
	"			vec4 v;\n" \
	"			uint label = uint(texture(tex0, t.stp).x*base.loc5.w+0.5); //get mask value\n" \
	"			vec4 c = texture(tex7, vec2((float(label%256)+0.5)/256.0, (float(label/256)+0.5)/256.0));\n" \
	"			c.rgb = c.rgb*base.loc6.z;\n" \
	"\n"

#define VRAY_DATA_VOLUME_LOOKUP \
	"			//VRAY_DATA_VOLUME_LOOKUP\n" \
	"			vec4 v = texture(tex0, t.stp);\n" \
    "           float orig = v.x;\n" \
	"\n"
    
#define VRAY_DATA_LABEL_SEG_IF \
    "           //VRAY_DATA_LABEL_SEG_LOOKUP\n" \
    "           uint lbl = texture(tex3, t.stp).x; //get label value\n" \
    "           vec4 b = texture(tex7, vec2((float(lbl%uint(256))+0.5)/256.0, (float(lbl/256)+0.5)/256.0));\n" \
	"           highlight = (b.x >= 0.5) ? 1.0 : highlight;\n" \
    "           if (b.x > 0.1) {\n" \
    "\n"
    
#define VRAY_DATA_LABEL_SEG_ENDIF \
    "           }\n"

#define VRAY_GRAD_COMPUTE_LO \
	"			//VRAY_GRAD_COMPUTE_LO\n" \
	"			vec4 r, p; \n" \
	"			v = vec4(v.x); \n" \
	"			n = vec4(0.0); \n" \
	"			w = vec4(0.0);\n" \
	"			w.x = dir.x; \n" \
	"			p = clamp(TexCoord + w, 0.0, 1.0); \n" \
	"			r = texture(tex0, p.stp); \n" \
	"			n.x = v.x - r.x; \n" \
	"			w = vec4(0.0); \n" \
	"			w.y = dir.y; \n" \
	"			p = clamp(TexCoord + w, 0.0, 1.0); \n" \
	"			r = texture(tex0, p.stp); \n" \
	"			n.y = v.y - r.x; \n" \
	"			w = vec4(0.0); \n" \
	"			w.z = dir.z; \n" \
	"			p = clamp(TexCoord + w, 0.0, 1.0); \n" \
	"			r = texture(tex0, p.stp); \n" \
	"			n.z = v.z - r.x; \n" \
	"			p.y = length(n.xyz); \n" \
	"			p.y = 0.5 * (base.loc2.x<0.0?(1.0+p.y*base.loc2.x):p.y*base.loc2.x); \n" \
	"\n"

#define VRAY_GRAD_COMPUTE_HI \
	"			// VOL_GRAD_COMPUTE_HI\n" \
	"			vec4 r, p;\n" \
	"			v = vec4(v.x);\n" \
	"			n = vec4(0.0);\n" \
	"			w = vec4(0.0);\n" \
	"			w.x = dir.x;\n" \
	"			p = clamp(TexCoord + w, 0.0, 1.0);\n" \
	"			r = texture(tex0, p.stp);\n" \
	"			n.x = r.x + n.x;\n" \
	"			p = clamp(TexCoord - w, 0.0, 1.0);\n" \
	"			r = texture(tex0, p.stp);\n" \
	"			n.x = r.x - n.x;\n" \
	"			w = vec4(0.0);\n" \
	"			w.y = dir.y;\n" \
	"			p = clamp(TexCoord + w, 0.0, 1.0);\n" \
	"			r = texture(tex0, p.stp);\n" \
	"			n.y = r.x + n.y;\n" \
	"			p = clamp(TexCoord - w, 0.0, 1.0);\n" \
	"			r = texture(tex0, p.stp);\n" \
	"			n.y = r.x - n.y;\n" \
	"			w = vec4(0.0);\n" \
	"			w.z = dir.z;\n" \
	"			p = clamp(TexCoord + w, 0.0, 1.0);\n" \
	"			r = texture(tex0, p.stp);\n" \
	"			n.z = r.x + n.z;\n" \
	"			p = clamp(TexCoord - w, 0.0, 1.0);\n" \
	"			r = texture(tex0, p.stp);\n" \
	"			n.z = r.x - n.z;\n" \
	"			p.y = length(n.xyz);\n" \
	"			p.y = 0.5 * (base.loc2.x < 0.0 ? (1.0 + p.y * base.loc2.x) : p.y * base.loc2.x);\n" \
	"\n"

#define VRAY_BODY_SHADING \
	"			//VRAY_BODY_SHADING\n" \
	"			n.xyz = normalize(n.xyz);\n" \
	"			n.w = dot(l.xyz, n.xyz); // calculate angle between light and normal. \n" \
	"			n.w = clamp(abs(n.w), 0.0, 1.0); // two-sided lighting, n.w = abs(cos(angle))  \n" \
	"			w = k; // w.x = weight*ka, w.y = weight*kd, w.z = weight*ks \n" \
	"			w.x = k.x - w.y; // w.x = ka - kd*weight \n" \
	"			w.x = w.x + k.y; // w.x = ka + kd - kd*weight \n" \
	"			n.z = pow(n.w, k.w); // n.z = abs(cos(angle))^ns \n" \
	"			n.w = (n.w * w.y) + w.x; // n.w = abs(cos(angle))*kd+ka\n" \
	"			n.z = w.z * n.z; // n.z = weight*ks*abs(cos(angle))^ns \n" \
	"\n"

#define VRAY_COMPUTED_GM_LOOKUP \
	"			//VRAY_COMPUTED_GM_LOOKUP\n" \
	"			v.y = p.y;\n" \
	"\n"

#define VRAY_COMPUTED_GM_INVALIDATE \
	"			//VRAY_COMPUTED_GM_INVALIDATE\n" \
	"			v.y = base.loc3.y;\n" \
	"\n"

#define VRAY_TEXTURE_GM_LOOKUP \
	"			//VRAY_TEXTURE_GM_LOOKUP\n" \
	"			v.y = texture(tex1, t.stp).x;\n" \
	"\n"

#define VRAY_TRANSFER_FUNCTION_SIN_COLOR \
	"			//VRAY_TRANSFER_FUNCTION_SIN_COLOR\n" \
	"			vec4 c;\n" \
	"			float tf_alp = 0.0;\n" \
	"			float alpha = 0.0;\n" \
	"			v.x = base.loc2.x < 0.0 ? (1.0 + v.x * base.loc2.x) : v.x * base.loc2.x;\n" \
	"			if (v.x<base.loc2.z - base.loc3.w || v.x>base.loc2.w + base.loc3.w || v.y < base.loc3.y - base.loc3.w)\n" \
	"				c = vec4(0.0);\n" \
	"			else\n" \
	"			{\n" \
	"				v.x = (v.x < base.loc2.z ? (base.loc3.w - base.loc2.z + v.x) / base.loc3.w : (v.x > base.loc2.w ? (base.loc3.w - v.x + base.loc2.w) / base.loc3.w : 1.0)) * v.x;\n" \
	"				v.x = (v.y < base.loc3.y ? (base.loc3.w - base.loc3.y + v.y) / base.loc3.w : 1.0) * v.x;\n" \
	"				tf_alp = pow(clamp(v.x / base.loc3.z,\n" \
	"					base.loc3.x < 1.0 ? -(base.loc3.x - 1.0) * 0.00001 : 0.0,\n" \
	"					base.loc3.x>1.0 ? 0.9999 : 1.0), base.loc3.x);\n" \
	"				alpha = (1.0 - pow(clamp(1.0 - tf_alp * l.w, 0.0, 1.0), base.loc7.z)) / l.w;\n" \
	"				c = vec4(base.loc6.rgb * alpha * tf_alp, alpha);\n" \
	"			}\n" \
	"\n" \

#define VRAY_TRANSFER_FUNCTION_SIN_COLOR_SOLID \
"            //VRAY_TRANSFER_FUNCTION_SIN_COLOR_SOLID\n" \
"            vec4 c;\n" \
"            float tf_alp = 0.0;\n" \
"            float alpha = 0.0;\n" \
"            v.x = base.loc2.x<0.0?(1.0+v.x*base.loc2.x):v.x*base.loc2.x;\n" \
"            if (v.x<base.loc2.z-base.loc3.w || v.x>base.loc2.w+base.loc3.w || v.y<base.loc3.y)\n" \
"                c = vec4(0.0, 0.0, 0.0, 1.0);\n" \
"            else\n" \
"            {\n" \
"                v.x = (v.x<base.loc2.z?(base.loc3.w-base.loc2.z+v.x)/base.loc3.w:(v.x>base.loc2.w?(base.loc3.w-v.x+base.loc2.w)/base.loc3.w:1.0))*v.x;\n" \
"                v.x = (v.y < base.loc3.y ? (base.loc3.w - base.loc3.y + v.y) / base.loc3.w : 1.0) * v.x;\n" \
"                tf_alp = pow(clamp(v.x/base.loc3.z,\n" \
"                    base.loc3.x<1.0?-(base.loc3.x-1.0)*0.00001:0.0,\n" \
"                    base.loc3.x>1.0?0.9999:1.0), base.loc3.x);\n" \
"                alpha = 1.0 - clamp(1.0 - tf_alp * l.w, 0.0, 1.0);\n" \
"                c = vec4(base.loc6.rgb*alpha*tf_alp, 1.0);\n" \
"            }\n" \
"\n"
    
/*
#define VRAY_TRANSFER_FUNCTION_SIN_COLOR_SOLID \
	"			//VRAY_TRANSFER_FUNCTION_SIN_COLOR_SOLID\n" \
	"			vec4 c;\n" \
	"			float tf_alp = 0.0;\n" \
	"			float alpha = 0.0;\n" \
	"			v.x = base.loc2.x<0.0?(1.0+v.x*base.loc2.x):v.x*base.loc2.x;\n" \
	"			if (v.x<base.loc2.z-base.loc3.w || v.x>base.loc2.w+base.loc3.w || v.y<base.loc3.y)\n" \
	"				c = vec4(0.0, 0.0, 0.0, 1.0);\n" \
	"			else\n" \
	"			{\n" \
	"				v.x = (v.x<base.loc2.z?(base.loc3.w-base.loc2.z+v.x)/base.loc3.w:(v.x>base.loc2.w?(base.loc3.w-v.x+base.loc2.w)/base.loc3.w:1.0))*v.x;\n" \
	"				tf_alp = pow(clamp(v.x/base.loc3.z,\n" \
	"					base.loc3.x<1.0?-(base.loc3.x-1.0)*0.00001:0.0,\n" \
	"					base.loc3.x>1.0?0.9999:1.0), base.loc3.x);\n" \
	"				c = vec4(base.loc6.rgb*tf_alp, 1.0);\n" \
	"			}\n" \
	"\n"
*/
    
#define VRAY_TRANSFER_FUNCTION_COLORMAP \
	"			//VRAY_TRANSFER_FUNCTION_COLORMAP\n" \
	"			vec4 c;\n" \
	"			float tf_alp = 0.0;\n" \
	"			float alpha = 0.0;\n" \
	"			v.x = base.loc2.x<0.0?(1.0+v.x*base.loc2.x):v.x*base.loc2.x;\n" \
	"			if (v.x<base.loc2.z-base.loc3.w || v.x>base.loc2.w+base.loc3.w || v.y<base.loc3.y)\n" \
	"				c = vec4(0.0);\n" \
	"			else\n" \
	"			{\n" \
	"				v.x = (v.x<base.loc2.z?(base.loc3.w-base.loc2.z+v.x)/base.loc3.w:(v.x>base.loc2.w?(base.loc3.w-v.x+base.loc2.w)/base.loc3.w:1.0))*v.x;\n" \
	"				vec4 rb = vec4(0.0);\n"

#define VRAY_TRANSFER_FUNCTION_COLORMAP_SOLID \
	"			//VRAY_TRANSFER_FUNCTION_COLORMAP_SOLID\n" \
	"			vec4 c;\n" \
	"			float tf_alp = 0.0;\n" \
	"			float alpha = 0.0;\n" \
	"			v.x = base.loc2.x<0.0?(1.0+v.x*base.loc2.x):v.x*base.loc2.x;\n" \
	"			if (v.x<base.loc2.z-base.loc3.w || v.x>base.loc2.w+base.loc3.w || v.y<base.loc3.y)\n" \
	"				c = vec4(0.0, 0.0, 0.0, 1.0);\n" \
	"			else\n" \
	"			{\n" \
	"				v.x = (v.x<base.loc2.z?(base.loc3.w-base.loc2.z+v.x)/base.loc3.w:(v.x>base.loc2.w?(base.loc3.w-v.x+base.loc2.w)/base.loc3.w:1.0))*v.x;\n" \
	"				vec4 rb = vec4(0.0);\n"

#define VRAY_TRANSFER_FUNCTION_COLORMAP_VALU0 \
	"				//VRAY_TRANSFER_FUNCTION_COLORMAP_VALU\n" \
	"				float valu = (v.x-base.loc6.x)/base.loc6.z;\n"

#define VRAY_TRANSFER_FUNCTION_COLORMAP_VALU1 \
	"				//VRAY_TRANSFER_FUNCTION_COLORMAP_VALU_Z\n" \
	"				vec4 tt = t*brk.brkscale + brk.brktrans;\n" \
	"				float valu = (1.0-tt.z-base.loc6.x)/base.loc6.z;\n"

#define VRAY_TRANSFER_FUNCTION_COLORMAP_VALU2 \
	"				//VRAY_TRANSFER_FUNCTION_COLORMAP_VALU_Z\n" \
	"				vec4 tt = t*brk.brkscale + brk.brktrans;\n" \
	"				float valu = (1.0-tt.y-base.loc6.x)/base.loc6.z;\n"

#define VRAY_TRANSFER_FUNCTION_COLORMAP_VALU3 \
	"				//VRAY_TRANSFER_FUNCTION_COLORMAP_VALU_Z\n" \
	"				vec4 tt = t*brk.brkscale + brk.brktrans;\n" \
	"				float valu = (1.0-tt.x-base.loc6.x)/base.loc6.z;\n"

#define VRAY_COLORMAP_CALC0 \
	"				//VRAY_COLORMAP_CALC0\n" \
	"				rb.r = clamp(4.0*valu - 2.0, 0.0, 1.0);\n" \
	"				rb.g = clamp(valu<0.5 ? 4.0*valu : -4.0*valu+4.0, 0.0, 1.0);\n" \
	"				rb.b = clamp(-4.0*valu+2.0, 0.0, 1.0);\n"

#define VRAY_COLORMAP_CALC1 \
	"				//VRAY_COLORMAP_CALC1\n" \
	"				rb.r = clamp(-4.0*valu+2.0, 0.0, 1.0);\n" \
	"				rb.g = clamp(valu<0.5 ? 4.0*valu : -4.0*valu+4.0, 0.0, 1.0);\n" \
	"				rb.b = clamp(4.0*valu - 2.0, 0.0, 1.0);\n"

#define VRAY_COLORMAP_CALC2 \
	"				//VRAY_COLORMAP_CALC2\n" \
	"				rb.r = clamp(2.0*valu, 0.0, 1.0);\n" \
	"				rb.g = clamp(4.0*valu - 2.0, 0.0, 1.0);\n" \
	"				rb.b = clamp(4.0*valu - 3.0, 0.0, 1.0);\n"

#define VRAY_COLORMAP_CALC3 \
	"				//VRAY_COLORMAP_CALC3\n" \
	"				rb.r = clamp(valu, 0.0, 1.0);\n" \
	"				rb.g = clamp(1.0-valu, 0.0, 1.0);\n" \
	"				rb.b = 1.0;\n"

#define VRAY_COLORMAP_CALC4 \
	"				//VRAY_COLORMAP_CALC4\n" \
	"				rb.r = clamp(valu<0.5?valu*0.9+0.25:0.7, 0.0, 1.0);\n" \
	"				rb.g = clamp(valu<0.5?valu*0.8+0.3:(1.0-valu)*1.4, 0.0, 1.0);\n" \
	"				rb.b = clamp(valu<0.5?valu*(-0.1)+0.75:(1.0-valu)*1.1+0.15, 0.0, 1.0);\n"

#define VRAY_TRANSFER_FUNCTION_COLORMAP_RESULT \
	"				//VRAY_TRANSFER_FUNCTION_COLORMAP_RESULT\n" \
	"				float alpha = (1.0 - pow(clamp(1.0-tf_alp*l.w, 0.0, 1.0), base.loc7.z)) / l.w;\n" \
	"				c = vec4(rb.rgb*alpha*tf_alp, alpha);\n" \
	"			}\n" \
	"\n"

#define VRAY_TRANSFER_FUNCTION_COLORMAP_SOLID_RESULT \
	"				//VRAY_TRANSFER_FUNCTION_COLORMAP_SOLID_RESULT\n" \
	"				c = vec4(rb.rgb, 1.0);\n" \
	"			}\n" \
	"\n"

#define VRAY_COMMON_TRANSFER_FUNCTION_CALC \
	"				//VRAY_COMMON_TRANSFER_FUNCTION_CALC\n" \
	"				tf_alp = pow(clamp(v.x/base.loc3.z,\n" \
	"					base.loc3.x<1.0?-(base.loc3.x-1.0)*0.00001:0.0,\n" \
	"					base.loc3.x>1.0?0.9999:1.0), base.loc3.x);\n"

#define VRAY_TRANSFER_FUNCTION_DEPTHMAP \
	"			//VRAY_TRANSFER_FUNCTION_DEPTHMAP\n" \
	"			vec4 c;\n" \
	"			float tf_alp = 0.0;\n" \
	"			float alpha = 0.0;\n" \
	"			v.x = base.loc2.x<0.0?(1.0+v.x*base.loc2.x):v.x*base.loc2.x;\n" \
	"			if (v.x<base.loc2.z-base.loc3.w || v.x>base.loc2.w+base.loc3.w || v.y<base.loc3.y)\n" \
	"				c = vec4(0.0);\n" \
	"			else\n" \
	"			{\n" \
	"				v.x = (v.x<base.loc2.z?(base.loc3.w-base.loc2.z+v.x)/base.loc3.w:(v.x>base.loc2.w?(base.loc3.w-v.x+base.loc2.w)/base.loc3.w:1.0))*v.x;\n" \
	"				tf_alp = pow(clamp(v.x/base.loc3.z,\n" \
	"					base.loc3.x<1.0?-(base.loc3.x-1.0)*0.00001:0.0,\n" \
	"					base.loc3.x>1.0?0.9999:1.0), base.loc3.x);\n" \
	"				alpha = tf_alp;\n" \
	"				c = vec4(vec3(alpha*tf_alp), alpha);\n" \
	"			}\n" \
	"\n"

#define VRAY_COLOR_OUTPUT \
	"			//VRAY_COLOR_OUTPUT\n" \
	"			c.xyz = c.xyz * clamp(1.0 - base.loc1.x, 0.0, 1.0) + base.loc1.x * c.xyz * (base.loc1.y > 0.0 ? (n.w + n.z) : 1.0);\n" \
	"			c.xyz *= pow(1.0 - base.loc1.x / 2.0, 2.0) + 1.0;\n" \
	"\n"

#define VRAY_FOG_BODY \
	"			//VRAY_FOG_BODY\n" \
	"			vec4 fogcoord = mv_mat * vec4(t.xyz * brk.brkscale.xyz + brk.brktrans.xyz, 1.0);\n" \
	"			fp.w = abs(fogcoord.z/fogcoord.w);\n" \
	"			v.x = (fp.z-fp.w)/(fp.z-fp.y);\n" \
	"			v.x = 1.0-clamp(v.x, 0.0, 1.0);\n" \
	"			v.x = 1.0-exp(-pow(v.x*2.5, 2.0));\n" \
	"			c.xyz = mix(c.xyz, vec3(base.loc9.x, base.loc9.y, base.loc9.z) * alpha * tf_alp, v.x*fp.x); \n" \
	"\n"

#define VRAY_RASTER_BLEND \
	"			//VRAY_RASTER_BLEND\n" \
	"			c = c*l.w; // VOL_RASTER_BLEND\n" \
	"\n"

#define VRAY_RASTER_BLEND_SOLID \
	"			//VRAY_RASTER_BLEND_SOLID\n" \
	"			//do nothing\n" \
	"\n"

#define VRAY_RASTER_BLEND_ID \
	"			//VRAY_RASTER_BLEND_ID\n" \
	"			c = c*l.w;\n" \
	"\n"

#define VRAY_RASTER_BLEND_DMAP \
	"			//VRAY_RASTER_BLEND_DMAP\n" \
	"			float intpo = (c*l.w).r;\n" \
	"			c = intpo > 0.05 ? vec4(vec3(cspv.z), 1.0) : vec4(0.0);\n" \
	"			prevz = intpo > 0.05 ? cspv.z : prevz;\n" \
	"\n"

#define VRAY_RASTER_BLEND_NOMASK \
	"			//VRAY_RASTER_BLEND_NOMASK\n" \
	"			vec4 cmask = texture(tex2, t.stp); //get mask value\n" \
	"			c = vec4(1.0-cmask.x)*c*l.w;\n" \
	"\n"

#define VRAY_RASTER_BLEND_NOMASK_SOLID \
	"			//VRAY_RASTER_BLEND_NOMASK_SOLID\n" \
	"			vec4 cmask = texture(tex2, t.stp); //get mask value\n" \
	"			c = vec4(1.0-cmask.x)*c;\n" \
	"\n"

#define VRAY_RASTER_BLEND_NOMASK_ID \
	"			//VRAY_RASTER_BLEND_NOMASK_ID\n" \
	"			vec4 cmask = texture(tex2, t.stp); //get mask value\n" \
	"			c = vec4(1.0-cmask.x)*c*l.w;\n" \
	"\n"

#define VRAY_RASTER_BLEND_NOMASK_DMAP \
	"			//VRAY_RASTER_BLEND_NOMASK_DMAP\n" \
	"			vec4 cmask = texture(tex2, t.stp); //get mask value\n" \
	"			float intpo = (vec4(1.0-cmask.x)*c*l.w).r;\n" \
	"			c = intpo > 0.05 ? vec4(vec3(cspv.z), 1.0) : vec4(0.0);\n" \
	"			prevz = intpo > 0.05 ? cspv.z : prevz;\n" \
	"\n"

#define VRAY_RASTER_BLEND_MASK \
	"			//VRAY_RASTER_BLEND_MASK\n" \
	"			vec4 cmask = texture(tex2, t.stp); //get mask value\n" \
	"			c = orig<base.loc6.w?vec4(0.0):vec4(cmask.x*0.0001)+vec4(cmask.x)*c*l.w;\n" \
	"\n"

#define VRAY_RASTER_BLEND_MASK_SOLID \
	"			//VRAY_RASTER_BLEND_MASK_SOLID\n" \
	"			vec4 cmask = texture(tex2, t.stp); //get mask value\n" \
	"			c = orig<base.loc6.w?vec4(0.0):vec4(cmask.x)*c;\n" \
	"\n"

#define VRAY_RASTER_BLEND_MASK_ID \
	"			//VRAY_RASTER_BLEND_MASK_ID\n" \
	"			vec4 cmask = texture(tex2, t.stp); //get mask value\n" \
	"			c = vec4(cmask.x)*c*l.w;\n" \
	"\n"

#define VRAY_RASTER_BLEND_MASK_DMAP \
	"			//VRAY_RASTER_BLEND_MASK_DMAP\n" \
	"			vec4 cmask = texture(tex2, t.stp); //get mask value\n" \
	"			float intpo = (vec4(cmask.x)*c*l.w).r;\n" \
	"			c = intpo > 0.05 ? vec4(vec3(cspv.z), 1.0) : vec4(0.0);\n" \
	"			prevz = intpo > 0.05 ? cspv.z : prevz;\n" \
	"\n"

#define VRAY_RASTER_BLEND_LABEL \
	"			//VRAY_RASTER_BLEND_LABEL\n" \
	"			uint label = texture(tex3, t.stp).x; //get mask value\n" \
	"			vec4 sel = vec4(0.2,\n" \
	"							0.4,\n" \
	"							0.4, 1.0);\n" \
	"			float hue, p2, p3;\n" \
	"			if (label > uint(0))\n" \
	"			{\n" \
	"				hue = float(label%uint(360))/60.0;\n" \
	"				p2 = 1.0 - hue + floor(hue);\n" \
	"				p3 = hue - floor(hue);\n" \
	"				if (hue < 1.0)\n" \
	"					sel = vec4(1.0, p3, 1.0, 1.0);\n" \
	"				else if (hue < 2.0)\n" \
	"					sel = vec4(p2, 1.0, 1.0, 1.0);\n" \
	"				else if (hue < 3.0)\n" \
	"					sel = vec4(1.0, 1.0, p3, 1.0);\n" \
	"				else if (hue < 4.0)\n" \
	"					sel = vec4(1.0, p2, 1.0, 1.0);\n" \
	"				else if (hue < 5.0)\n" \
	"					sel = vec4(p3, 1.0, 1.0, 1.0);\n" \
	"				else\n" \
	"					sel = vec4(1.0, 1.0, p2, 1.0);\n" \
	"			}\n" \
	"			sel.xyz = sel.xyz*clamp(1.0-base.loc1.x, 0.0, 1.0) + base.loc1.x*sel.xyz*(base.loc1.y > 0.0?(n.w + n.z):1.0);\n" \
	"			sel.xyz *= pow(1.0 - base.loc1.x / 2.0, 2.0) + 1.0;\n" \
	"			c = sel*l.w;\n" \
	"\n"

#define VRAY_RASTER_BLEND_LABEL_MASK \
	"			//VRAY_RASTER_BLEND_LABEL_MASK\n" \
	"			vec4 cmask = texture(tex2, t.stp); //get mask value\n" \
	"			if (cmask.x <= base.loc6.w)\n" \
	"			{\n" \
	"				c = c*l.w;\n" \
	"			}\n" \
	"			else \n" \
	"			{\n" \
	"				uint label = texture(tex3, t.stp).x; //get mask value\n" \
	"				vec4 sel = vec4(0.1,\n" \
	"								0.2,\n" \
	"								0.2, 0.5);\n" \
	"				float hue, p2, p3;\n" \
	"				if (label > uint(0))\n" \
	"				{\n" \
	"					hue = float(label%uint(360))/60.0;\n" \
	"					p2 = 1.0 - hue + floor(hue);\n" \
	"					p3 = hue - floor(hue);\n" \
	"					if (hue < 1.0)\n" \
	"						sel = vec4(1.0, p3, 0.0, 1.0);\n" \
	"					else if (hue < 2.0)\n" \
	"						sel = vec4(p2, 1.0, 0.0, 1.0);\n" \
	"					else if (hue < 3.0)\n" \
	"						sel = vec4(0.0, 1.0, p3, 1.0);\n" \
	"					else if (hue < 4.0)\n" \
	"						sel = vec4(0.0, p2, 1.0, 1.0);\n" \
	"					else if (hue < 5.0)\n" \
	"						sel = vec4(p3, 0.0, 1.0, 1.0);\n" \
	"					else\n" \
	"						sel = vec4(1.0, 0.0, p2, 1.0);\n" \
	"				}\n" \
	"				sel.xyz = sel.xyz*clamp(1.0-base.loc1.x, 0.0, 1.0) + base.loc1.x*sel.xyz*(base.loc1.y > 0.0?(n.w + n.z):1.0);\n" \
	"				sel.xyz *= pow(1.0 - base.loc1.x / 2.0, 2.0) + 1.0;\n" \
	"				c = sel*alpha*tf_alp*l.w;\n" \
	"			}\n" \
	"\n"

#define VRAY_BLEND_OVER \
	"			//VRAY_BLEND_OVER\n" \
	"			outcol = outcol + (1.0 - outcol.w) * c;\n" \
	"\n"

#define VRAY_BLEND_MIP \
	"			//VRAY_BLEND_MIP\n" \
	"			if (v.x > maxval)\n" \
	"			{\n" \
	"				maxval = v.x;\n" \
	"				outcol = c;\n" \
	"			}\n" \
	"\n"

#define VRAY_BLEND_DMAP \
	"			//VRAY_BLEND_DMAP\n" \
	"			outcol = (1.0 - c.w) * outcol + c;\n" \
	"\n"

#define VRAY_LOOP_TAIL \
    "        }\n" \
    "    }\n" \
    "\n"

#define VRAY_HIGHLIGHT_ADD \
    "    outcol.rgb = highlight ? clamp(outcol.rgb + hcol.rgb*hcol.w, 0.0, 1.0) : outcol.rgb;\n" \
    "\n"

#define VRAY_ASSIGN_COLOR_LABEL_SEG \
	"	FragColor = outcol + highlight * 0.1 * clamp(outcol, 0.01, 1.0);\n" \
	"\n"

#define VRAY_ASSIGN_COLOR_NA_MASK \
    "    FragColor = outcol * 0.001;\n" \
    "\n"
    
#define VRAY_VRAY_ASSIGN_COLOR \
	"	FragColor = outcol;\n" \
	"\n" 

#define VRAY_LOOP_TAIL_INDEX_COLOR \
	"		}\n" \
	"		else\n" \
	"		{\n" \
	"			prev_id = 0;\n" \
	"		}\n" \
	"	}\n" \
	"\n" \
	"	FragColor = outcol;\n" \
	"\n" 

#define VRAY_TAIL \
	"	//VRAY_TAIL\n" \
	"}\n" 

#define VRAY_FRG_SHADER_CODE_TEST_PERSP \
	"#version 450\n" \
	"#pragma optionNV(inline all)\n" \
	"#pragma optionNV(fastmath on)\n" \
	"#pragma optionNV(ifcvt none)\n" \
	"#pragma optionNV(strict on)\n" \
	"#pragma optionNV(unroll all)\n" \
	"layout(location = 0) in vec4 OutVertex;\n" \
	"layout(location = 1) in vec3 OutTexture;\n" \
	"layout(location = 0) out vec4 FragColor;\n" \
	"// VOL_UNIFORMS_BASE\n" \
	"layout(binding = 1) uniform VolFragShaderBaseUBO {\n" \
	"	vec4 loc0;//(lx, ly, lz, alpha)\n" \
	"	vec4 loc1;//(ka, kd, ks, ns)\n" \
	"	vec4 loc2;//(scalar_scale, gm_scale, left_thresh, right_thresh)\n" \
	"	vec4 loc3;//(gamma, gm_thresh, offset, sw)\n" \
	"	vec4 loc5;//(spcx, spcy, spcz, max_id)\n" \
	"	vec4 loc6;//(r, g, b, 0.0) or (1/vx, 1/vy, luminance, depth_mode)\n" \
	"	vec4 loc7;//(1/vx, 1/vy, 1/sample_rate, 0.0)\n" \
	"	vec4 loc8;//(int, start, end, 0.0)\n" \
	"	vec4 loc10; //plane0\n" \
	"	vec4 loc11; //plane1\n" \
	"	vec4 loc12; //plane2\n" \
	"	vec4 loc13; //plane3\n" \
	"	vec4 loc14; //plane4\n" \
	"	vec4 loc15; //plane5\n" \
	"	mat4 matrix0; //inverted projection matrix\n" \
	"	mat4 matrix1; //inverted modelview matrix\n" \
	"} base;\n" \
	"\n" \
	"layout(binding = 2) uniform sampler3D tex0;//data volume\n" \
	"layout(binding = 3) uniform sampler3D tex1;//gm volume\n" \
	"\n" \
	"// VOL_UNIFORMS_BRICK\n" \
	"layout(push_constant) uniform VolFragShaderBrickConst {\n" \
	"	vec4 brkscale;//tex transform for bricking and 1/nx\n" \
	"	vec4 brktrans;//tex transform for bricking and 1/ny\n" \
	"	vec4 mskbrkscale;//tex transform for mask bricks and 1/nz\n" \
	"	vec4 mskbrktrans;//tex transform for mask bricks\n" \
	"	vec4 tbmin;//tex bbox min\n" \
	"	vec4 tbmax;//tex bbox max\n" \
	"	vec3 loc4;//(zmin, zmax, dz)\n" \
	"	uint stnum;\n" \
	"} brk;\n" \
	"\n" \
	"//VOL_CLIP_FUNC\n" \
	"bool vol_clip_func(vec4 t)\n" \
	"{\n" \
	"	vec3 brickt = (t.xyz * brk.brkscale.xyz + brk.brktrans.xyz);\n" \
	"	if (dot(brickt.xyz, base.loc10.xyz) + base.loc10.w < 0.0 ||\n" \
	"		dot(brickt.xyz, base.loc11.xyz) + base.loc11.w < 0.0 ||\n" \
	"		dot(brickt.xyz, base.loc12.xyz) + base.loc12.w < 0.0 ||\n" \
	"		dot(brickt.xyz, base.loc13.xyz) + base.loc13.w < 0.0 ||\n" \
	"		dot(brickt.xyz, base.loc14.xyz) + base.loc14.w < 0.0 ||\n" \
	"		dot(brickt.xyz, base.loc15.xyz) + base.loc15.w < 0.0)\n" \
	"		return true;\n" \
	"	else\n" \
	"		return false;\n" \
	"}\n" \
	"\n" \
	"//VOL_HEAD\n" \
	"void main()\n" \
	"{\n" \
	"	const vec3 vray = normalize(base.matrix0 * OutVertex).xyz;\n" \
	"	const vec4 ray = base.matrix1 * vec4(vray, 0.0) / vec4(brk.brkscale.xyz, 1.);\n" \
	"	const vec4 st = base.matrix1 * vec4((brk.loc4.x / vray.z) * vray, 1.0) / vec4(brk.brkscale.xyz, 1.) - vec4(brk.brktrans.xyz / brk.brkscale.xyz, 0.0);\n" \
	"	const float step = brk.loc4.z / vray.z;\n" \
	"	vec4 outcol = vec4(0.0);\n" \
	"	vec4 dir = vec4(brk.brkscale.w, brk.brktrans.w, brk.mskbrkscale.w, 0.0);\n" \
	"\n" \
	"	//VOL_HEAD_LIT\n" \
	"	vec4 l = base.loc0; // {lx, ly, lz, alpha}\n" \
	"	vec4 k = base.loc1; // {ka, kd, ks, ns}\n" \
	"	k.x = k.x > 1.0 ? log2(3.0 - k.x) : k.x;\n" \
	"	vec4 n, w;\n" \
	"	if (l.w == 0.0) { discard; return; }\n" \
	"\n" \
	"	for (uint i = 0; i < brk.stnum; i++)\n" \
	"	{\n" \
	"		vec4 TexCoord = st + ray*step*float(i);\n" \
	"		vec4 t = vec4(TexCoord.xyz, 1.0);\n" \
	"\n" \
	"		uint cond = vol_clip_func(t) ? 1 : 0;\n" \
	"		cond += t.x < brk.tbmin.x ? 1 : 0;\n" \
	"		cond += t.x > brk.tbmax.x ? 1 : 0;\n" \
	"		cond += t.y < brk.tbmin.y ? 1 : 0;\n" \
	"		cond += t.y > brk.tbmax.y ? 1 : 0;\n" \
	"		cond += t.z < brk.tbmin.z ? 1 : 0;\n" \
	"		cond += t.z > brk.tbmax.z ? 1 : 0;\n" \
	"		cond += outcol.w > 0.99995 ? 1 : 0;\n" \
	"		if (cond == 0)\n" \
	"		{\n" \
	"			//VOL_DATA_VOLUME_LOOKUP\n" \
	"			vec4 v = texture(tex0, t.stp);\n" \
	"\n" \
	"			// VOL_GRAD_COMPUTE_HI\n" \
	"			vec4 r, p;\n" \
	"			v = vec4(v.x);\n" \
	"			n = vec4(0.0);\n" \
	"			w = vec4(0.0);\n" \
	"			w.x = dir.x;\n" \
	"			p = clamp(TexCoord + w, 0.0, 1.0);\n" \
	"			r = texture(tex0, p.stp);\n" \
	"			n.x = r.x + n.x;\n" \
	"			p = clamp(TexCoord - w, 0.0, 1.0);\n" \
	"			r = texture(tex0, p.stp);\n" \
	"			n.x = r.x - n.x;\n" \
	"			w = vec4(0.0);\n" \
	"			w.y = dir.y;\n" \
	"			p = clamp(TexCoord + w, 0.0, 1.0);\n" \
	"			r = texture(tex0, p.stp);\n" \
	"			n.y = r.x + n.y;\n" \
	"			p = clamp(TexCoord - w, 0.0, 1.0);\n" \
	"			r = texture(tex0, p.stp);\n" \
	"			n.y = r.x - n.y;\n" \
	"			w = vec4(0.0);\n" \
	"			w.z = dir.z;\n" \
	"			p = clamp(TexCoord + w, 0.0, 1.0);\n" \
	"			r = texture(tex0, p.stp);\n" \
	"			n.z = r.x + n.z;\n" \
	"			p = clamp(TexCoord - w, 0.0, 1.0);\n" \
	"			r = texture(tex0, p.stp);\n" \
	"			n.z = r.x - n.z;\n" \
	"			p.y = length(n.xyz);\n" \
	"			p.y = 0.5 * (base.loc2.x < 0.0 ? (1.0 + p.y * base.loc2.x) : p.y * base.loc2.x);\n" \
	"\n" \
	"			//VOL_BODY_SHADING\n" \
	"			n.xyz = normalize(n.xyz);\n" \
	"			n.w = dot(l.xyz, n.xyz); // calculate angle between light and normal. \n" \
	"			n.w = clamp(abs(n.w), 0.0, 1.0); // two-sided lighting, n.w = abs(cos(angle))  \n" \
	"			w = k; // w.x = weight*ka, w.y = weight*kd, w.z = weight*ks \n" \
	"			w.x = k.x - w.y; // w.x = ka - kd*weight \n" \
	"			w.x = w.x + k.y; // w.x = ka + kd - kd*weight \n" \
	"			n.z = pow(n.w, k.w); // n.z = abs(cos(angle))^ns \n" \
	"			n.w = (n.w * w.y) + w.x; // n.w = abs(cos(angle))*kd+ka\n" \
	"			n.z = w.z * n.z; // n.z = weight*ks*abs(cos(angle))^ns \n" \
	"\n" \
	"			//VOL_COMPUTED_GM_LOOKUP\n" \
	"			v.y = p.y;\n" \
	"\n" \
	"			//VOL_TRANSFER_FUNCTION_SIN_COLOR\n" \
	"			vec4 c;\n" \
	"			float tf_alp = 0.0;\n" \
	"			float alpha = 0.0;\n" \
	"			v.x = base.loc2.x < 0.0 ? (1.0 + v.x * base.loc2.x) : v.x * base.loc2.x;\n" \
	"			if (v.x<base.loc2.z - base.loc3.w || v.x>base.loc2.w + base.loc3.w || v.y < base.loc3.y - base.loc3.w)\n" \
	"				c = vec4(0.0);\n" \
	"			else\n" \
	"			{\n" \
	"				v.x = (v.x < base.loc2.z ? (base.loc3.w - base.loc2.z + v.x) / base.loc3.w : (v.x > base.loc2.w ? (base.loc3.w - v.x + base.loc2.w) / base.loc3.w : 1.0)) * v.x;\n" \
	"				v.x = (v.y < base.loc3.y ? (base.loc3.w - base.loc3.y + v.y) / base.loc3.w : 1.0) * v.x;\n" \
	"				tf_alp = pow(clamp(v.x / base.loc3.z,\n" \
	"					base.loc3.x < 1.0 ? -(base.loc3.x - 1.0) * 0.00001 : 0.0,\n" \
	"					base.loc3.x>1.0 ? 0.9999 : 1.0), base.loc3.x);\n" \
	"				alpha = (1.0 - pow(clamp(1.0 - tf_alp * l.w, 0.0, 1.0), base.loc7.z)) / l.w;\n" \
	"				c = vec4(base.loc6.rgb * alpha * tf_alp, alpha);\n" \
	"			}\n" \
	"\n" \
	"			//VOL_COLOR_OUTPUT\n" \
	"			c.xyz = c.xyz * clamp(1.0 - base.loc1.x, 0.0, 1.0) + base.loc1.x * c.xyz * (base.loc1.y > 0.0 ? (n.w + n.z) : 1.0);\n" \
	"			c.xyz *= pow(1.0 - base.loc1.x / 2.0, 2.0) + 1.0;\n" \
	"\n" \
	"			//VOL_RASTER_BLEND\n" \
	"			c = c * l.w;\n" \
	"			outcol = outcol + (1.0 - outcol.w) * c; // VOL_RASTER_BLEND\n" \
	"		}\n" \
	"	}\n" \
	"\n" \
	"	FragColor = outcol;\n" \
	"\n" \
	"	//VOL_TAIL\n" \
	"}\n" 


#define VRAY_FRG_SHADER_CODE_TEST_ORTHO \
	"#version 450\n" \
	"#pragma optionNV(inline all)\n" \
	"#pragma optionNV(fastmath on)\n" \
	"#pragma optionNV(ifcvt none)\n" \
	"#pragma optionNV(strict on)\n" \
	"#pragma optionNV(unroll all)\n" \
	"layout(location = 0) in vec4 OutVertex;\n" \
	"layout(location = 1) in vec3 OutTexture;\n" \
	"layout(location = 0) out vec4 FragColor;\n" \
	"// VOL_UNIFORMS_BASE\n" \
	"layout(binding = 1) uniform VolFragShaderBaseUBO {\n" \
	"	vec4 loc0;//(lx, ly, lz, alpha)\n" \
	"	vec4 loc1;//(ka, kd, ks, ns)\n" \
	"	vec4 loc2;//(scalar_scale, gm_scale, left_thresh, right_thresh)\n" \
	"	vec4 loc3;//(gamma, gm_thresh, offset, sw)\n" \
	"	vec4 loc5;//(spcx, spcy, spcz, max_id)\n" \
	"	vec4 loc6;//(r, g, b, 0.0) or (1/vx, 1/vy, luminance, depth_mode)\n" \
	"	vec4 loc7;//(1/vx, 1/vy, 1/sample_rate, 0.0)\n" \
	"	vec4 loc8;//(int, start, end, 0.0)\n" \
	"	vec4 loc10; //plane0\n" \
	"	vec4 loc11; //plane1\n" \
	"	vec4 loc12; //plane2\n" \
	"	vec4 loc13; //plane3\n" \
	"	vec4 loc14; //plane4\n" \
	"	vec4 loc15; //plane5\n" \
	"	mat4 matrix0; //inverted projection matrix\n" \
	"	mat4 matrix1; //inverted modelview matrix\n" \
	"} base;\n" \
	"\n" \
	"layout(binding = 2) uniform sampler3D tex0;//data volume\n" \
	"layout(binding = 3) uniform sampler3D tex1;//gm volume\n" \
	"\n" \
	"// VOL_UNIFORMS_BRICK\n" \
	"layout(push_constant) uniform VolFragShaderBrickConst {\n" \
	"	vec4 brkscale;//tex transform for bricking and 1/nx\n" \
	"	vec4 brktrans;//tex transform for bricking and 1/ny\n" \
	"	vec4 mskbrkscale;//tex transform for mask bricks and 1/nz\n" \
	"	vec4 mskbrktrans;//tex transform for mask bricks\n" \
	"	vec4 tbmin;//tex bbox min\n" \
	"	vec4 tbmax;//tex bbox max\n" \
	"	vec3 loc4;//(zmin, zmax, dz)\n" \
	"	uint stnum;\n" \
	"} brk;\n" \
	"\n" \
	"//VOL_CLIP_FUNC\n" \
	"bool vol_clip_func(vec4 t)\n" \
	"{\n" \
	"	vec3 brickt = (t.xyz * brk.brkscale.xyz + brk.brktrans.xyz);\n" \
	"	if (dot(brickt.xyz, base.loc10.xyz) + base.loc10.w < 0.0 ||\n" \
	"		dot(brickt.xyz, base.loc11.xyz) + base.loc11.w < 0.0 ||\n" \
	"		dot(brickt.xyz, base.loc12.xyz) + base.loc12.w < 0.0 ||\n" \
	"		dot(brickt.xyz, base.loc13.xyz) + base.loc13.w < 0.0 ||\n" \
	"		dot(brickt.xyz, base.loc14.xyz) + base.loc14.w < 0.0 ||\n" \
	"		dot(brickt.xyz, base.loc15.xyz) + base.loc15.w < 0.0)\n" \
	"		return true;\n" \
	"	else\n" \
	"		return false;\n" \
	"}\n" \
	"\n" \
	"//VOL_HEAD\n" \
	"void main()\n" \
	"{\n" \
	"	vec4 view = vec4(0.0, 0.0, brk.loc4.x, 0.0);\n" \
	"	const vec4 ray = base.matrix1 * vec4(0.0, 0.0, 1.0, 0.0) / vec4(brk.brkscale.xyz, 1.);\n" \
	"	const vec4 st = base.matrix1 * vec4((base.matrix0 * OutVertex).xy, brk.loc4.x, 1.0) / vec4(brk.brkscale.xyz, 1.) - vec4(brk.brktrans.xyz / brk.brkscale.xyz, 0.0);\n" \
	"	const float step = brk.loc4.z;\n" \
	"	vec4 outcol = vec4(0.0);\n" \
	"	vec4 dir = vec4(brk.brkscale.w, brk.brktrans.w, brk.mskbrkscale.w, 0.0);\n" \
	"\n" \
	"	//VOL_HEAD_LIT\n" \
	"	vec4 l = base.loc0; // {lx, ly, lz, alpha}\n" \
	"	vec4 k = base.loc1; // {ka, kd, ks, ns}\n" \
	"	k.x = k.x > 1.0 ? log2(3.0 - k.x) : k.x;\n" \
	"	vec4 n, w;\n" \
	"	if (l.w == 0.0) { discard; return; }\n" \
	"\n" \
	"	for (uint i = 0; i < brk.stnum; i++)\n" \
	"	{\n" \
	"		vec4 TexCoord = st + ray*step*float(i);\n" \
	"		vec4 t = vec4(TexCoord.xyz, 1.0);\n" \
	"\n" \
	"		if (!vol_clip_func(t) && t.x >= brk.tbmin.x && t.x <= brk.tbmax.x && t.y >= brk.tbmin.y && t.y <= brk.tbmax.y && t.z >= brk.tbmin.z && t.z <= brk.tbmax.z && outcol.w <= 0.99995)\n" \
	"		{\n" \
	"			//VOL_DATA_VOLUME_LOOKUP\n" \
	"			vec4 v = texture(tex0, t.stp);\n" \
	"\n" \
	"			// VOL_GRAD_COMPUTE_HI\n" \
	"			vec4 r, p;\n" \
	"			v = vec4(v.x);\n" \
	"			n = vec4(0.0);\n" \
	"			w = vec4(0.0);\n" \
	"			w.x = dir.x;\n" \
	"			p = clamp(TexCoord + w, 0.0, 1.0);\n" \
	"			r = texture(tex0, p.stp);\n" \
	"			n.x = r.x + n.x;\n" \
	"			p = clamp(TexCoord - w, 0.0, 1.0);\n" \
	"			r = texture(tex0, p.stp);\n" \
	"			n.x = r.x - n.x;\n" \
	"			w = vec4(0.0);\n" \
	"			w.y = dir.y;\n" \
	"			p = clamp(TexCoord + w, 0.0, 1.0);\n" \
	"			r = texture(tex0, p.stp);\n" \
	"			n.y = r.x + n.y;\n" \
	"			p = clamp(TexCoord - w, 0.0, 1.0);\n" \
	"			r = texture(tex0, p.stp);\n" \
	"			n.y = r.x - n.y;\n" \
	"			w = vec4(0.0);\n" \
	"			w.z = dir.z;\n" \
	"			p = clamp(TexCoord + w, 0.0, 1.0);\n" \
	"			r = texture(tex0, p.stp);\n" \
	"			n.z = r.x + n.z;\n" \
	"			p = clamp(TexCoord - w, 0.0, 1.0);\n" \
	"			r = texture(tex0, p.stp);\n" \
	"			n.z = r.x - n.z;\n" \
	"			p.y = length(n.xyz);\n" \
	"			p.y = 0.5 * (base.loc2.x < 0.0 ? (1.0 + p.y * base.loc2.x) : p.y * base.loc2.x);\n" \
	"\n" \
	"			//VOL_BODY_SHADING\n" \
	"			n.xyz = normalize(n.xyz);\n" \
	"			n.w = dot(l.xyz, n.xyz); // calculate angle between light and normal. \n" \
	"			n.w = clamp(abs(n.w), 0.0, 1.0); // two-sided lighting, n.w = abs(cos(angle))  \n" \
	"			w = k; // w.x = weight*ka, w.y = weight*kd, w.z = weight*ks \n" \
	"			w.x = k.x - w.y; // w.x = ka - kd*weight \n" \
	"			w.x = w.x + k.y; // w.x = ka + kd - kd*weight \n" \
	"			n.z = pow(n.w, k.w); // n.z = abs(cos(angle))^ns \n" \
	"			n.w = (n.w * w.y) + w.x; // n.w = abs(cos(angle))*kd+ka\n" \
	"			n.z = w.z * n.z; // n.z = weight*ks*abs(cos(angle))^ns \n" \
	"\n" \
	"			//VOL_COMPUTED_GM_LOOKUP\n" \
	"			v.y = p.y;\n" \
	"\n" \
	"			//VOL_TRANSFER_FUNCTION_SIN_COLOR\n" \
	"			vec4 c;\n" \
	"			float tf_alp = 0.0;\n" \
	"			float alpha = 0.0;\n" \
	"			v.x = base.loc2.x < 0.0 ? (1.0 + v.x * base.loc2.x) : v.x * base.loc2.x;\n" \
	"			if (v.x<base.loc2.z - base.loc3.w || v.x>base.loc2.w + base.loc3.w || v.y < base.loc3.y - base.loc3.w)\n" \
	"				c = vec4(0.0);\n" \
	"			else\n" \
	"			{\n" \
	"				v.x = (v.x < base.loc2.z ? (base.loc3.w - base.loc2.z + v.x) / base.loc3.w : (v.x > base.loc2.w ? (base.loc3.w - v.x + base.loc2.w) / base.loc3.w : 1.0)) * v.x;\n" \
	"				v.x = (v.y < base.loc3.y ? (base.loc3.w - base.loc3.y + v.y) / base.loc3.w : 1.0) * v.x;\n" \
	"				tf_alp = pow(clamp(v.x / base.loc3.z,\n" \
	"					base.loc3.x < 1.0 ? -(base.loc3.x - 1.0) * 0.00001 : 0.0,\n" \
	"					base.loc3.x>1.0 ? 0.9999 : 1.0), base.loc3.x);\n" \
	"				alpha = (1.0 - pow(clamp(1.0 - tf_alp * l.w, 0.0, 1.0), base.loc7.z)) / l.w;\n" \
	"				c = vec4(base.loc6.rgb * alpha * tf_alp, alpha);\n" \
	"			}\n" \
	"\n" \
	"			//VOL_COLOR_OUTPUT\n" \
	"			c.xyz = c.xyz * clamp(1.0 - base.loc1.x, 0.0, 1.0) + base.loc1.x * c.xyz * (base.loc1.y > 0.0 ? (n.w + n.z) : 1.0);\n" \
	"			c.xyz *= pow(1.0 - base.loc1.x / 2.0, 2.0) + 1.0;\n" \
	"\n" \
	"			//VOL_RASTER_BLEND\n" \
	"			c = c * l.w;\n" \
	"			outcol = outcol + (1.0 - outcol.w) * c; // VOL_RASTER_BLEND\n" \
	"		}\n" \
	"	}\n" \
	"\n" \
	"	FragColor = outcol;\n" \
	"\n" \
	"	//VOL_TAIL\n" \
	"}\n" 

#define VRAY_FRG_SHADER_CODE_TEST_PERSP_UV \
	"#version 450\n" \
	"#pragma optionNV(inline all)\n" \
	"#pragma optionNV(fastmath on)\n" \
	"#pragma optionNV(ifcvt none)\n" \
	"#pragma optionNV(strict on)\n" \
	"#pragma optionNV(unroll all)\n" \
	"layout(location = 0) in vec4 OutVertex;\n" \
	"layout(location = 1) in vec3 OutTexture;\n" \
	"layout(location = 0) out vec4 FragColor;\n" \
	"// VOL_UNIFORMS_BASE\n" \
	"layout(binding = 1) uniform VolFragShaderBaseUBO {\n" \
	"	vec4 loc0;//(lx, ly, lz, alpha)\n" \
	"	vec4 loc1;//(ka, kd, ks, ns)\n" \
	"	vec4 loc2;//(scalar_scale, gm_scale, left_thresh, right_thresh)\n" \
	"	vec4 loc3;//(gamma, gm_thresh, offset, sw)\n" \
	"	vec4 loc5;//(spcx, spcy, spcz, max_id)\n" \
	"	vec4 loc6;//(r, g, b, 0.0) or (1/vx, 1/vy, luminance, depth_mode)\n" \
	"	vec4 loc7;//(1/vx, 1/vy, 0.0, 0.0)\n" \
	"	vec4 loc8;//(int, start, end, 0.0)\n" \
	"	vec4 loc10; //plane0\n" \
	"	vec4 loc11; //plane1\n" \
	"	vec4 loc12; //plane2\n" \
	"	vec4 loc13; //plane3\n" \
	"	vec4 loc14; //plane4\n" \
	"	vec4 loc15; //plane5\n" \
	"	mat4 matrix0; //inverted projection matrix\n" \
	"	mat4 matrix1; //inverted modelview matrix\n" \
	"} base;\n" \
	"\n" \
	"layout(binding = 2) uniform sampler3D tex0;//data volume\n" \
	"layout(binding = 3) uniform sampler3D tex1;//gm volume\n" \
	"\n" \
	"// VOL_UNIFORMS_BRICK\n" \
	"layout(push_constant) uniform VolFragShaderBrickConst {\n" \
	"	vec4 brkscale;//tex transform for bricking and 1/nx\n" \
	"	vec4 brktrans;//tex transform for bricking and 1/ny\n" \
	"	vec4 mskbrkscale;//tex transform for mask bricks and 1/nz\n" \
	"	vec4 mskbrktrans;//tex transform for mask bricks\n" \
	"	vec4 tbmin;//tex bbox min\n" \
	"	vec4 tbmax;//tex bbox max\n" \
	"	vec3 loc4;//(zmin, zmax, dz)\n" \
	"	uint stnum;\n" \
	"} brk;\n" \
	"\n" \
	"//VOL_CLIP_FUNC\n" \
	"bool vol_clip_func(vec4 t)\n" \
	"{\n" \
	"	vec3 brickt = (t.xyz * brk.brkscale.xyz + brk.brktrans.xyz);\n" \
	"	if (dot(brickt.xyz, base.loc10.xyz) + base.loc10.w < 0.0 ||\n" \
	"		dot(brickt.xyz, base.loc11.xyz) + base.loc11.w < 0.0 ||\n" \
	"		dot(brickt.xyz, base.loc12.xyz) + base.loc12.w < 0.0 ||\n" \
	"		dot(brickt.xyz, base.loc13.xyz) + base.loc13.w < 0.0 ||\n" \
	"		dot(brickt.xyz, base.loc14.xyz) + base.loc14.w < 0.0 ||\n" \
	"		dot(brickt.xyz, base.loc15.xyz) + base.loc15.w < 0.0)\n" \
	"		return true;\n" \
	"	else\n" \
	"		return false;\n" \
	"}\n" \
	"\n" \
	"//VOL_HEAD\n" \
	"void main()\n" \
	"{\n" \
	"	const vec3 vray = normalize(base.matrix0 * OutVertex).xyz;\n" \
	"	const vec4 ray = normalize(base.matrix1 * vec4(vray, 0.0) / vec4(brk.brkscale.xyz, 1.));\n" \
	"	const vec4 st = base.matrix1 * vec4((brk.loc4.x / vray.z) * vray, 1.0) / vec4(brk.brkscale.xyz, 1.) - vec4(brk.brktrans.xyz / brk.brkscale.xyz, 0.0);\n" \
	"	const float step = brk.loc4.z / vray.z;\n" \
	"	vec4 outcol = vec4(0.0);\n" \
	"	const vec4 dir = vec4(brk.brkscale.w, brk.brktrans.w, brk.mskbrkscale.w, 0.0);\n" \
	"\n" \
	"	vec4 TexCoord = st + ray*step*float(0);\n" \
	"	vec4 t = vec4(TexCoord.xyz, 1.0);\n" \
	"	FragColor = t;\n" \
	"\n" \
	"	//VOL_TAIL\n" \
	"}\n" 

#define VRAY_FRG_SHADER_CODE_TEST_ORTHO_UV \
	"#version 450\n" \
	"#pragma optionNV(inline all)\n" \
	"#pragma optionNV(fastmath on)\n" \
	"#pragma optionNV(ifcvt none)\n" \
	"#pragma optionNV(strict on)\n" \
	"#pragma optionNV(unroll all)\n" \
	"layout(location = 0) in vec4 OutVertex;\n" \
	"layout(location = 1) in vec3 OutTexture;\n" \
	"layout(location = 0) out vec4 FragColor;\n" \
	"// VOL_UNIFORMS_BASE\n" \
	"layout(binding = 1) uniform VolFragShaderBaseUBO {\n" \
	"	vec4 loc0;//(lx, ly, lz, alpha)\n" \
	"	vec4 loc1;//(ka, kd, ks, ns)\n" \
	"	vec4 loc2;//(scalar_scale, gm_scale, left_thresh, right_thresh)\n" \
	"	vec4 loc3;//(gamma, gm_thresh, offset, sw)\n" \
	"	vec4 loc5;//(spcx, spcy, spcz, max_id)\n" \
	"	vec4 loc6;//(r, g, b, 0.0) or (1/vx, 1/vy, luminance, depth_mode)\n" \
	"	vec4 loc7;//(1/vx, 1/vy, 0.0, 0.0)\n" \
	"	vec4 loc8;//(int, start, end, 0.0)\n" \
	"	vec4 loc10; //plane0\n" \
	"	vec4 loc11; //plane1\n" \
	"	vec4 loc12; //plane2\n" \
	"	vec4 loc13; //plane3\n" \
	"	vec4 loc14; //plane4\n" \
	"	vec4 loc15; //plane5\n" \
	"	mat4 matrix0; //inverted projection matrix\n" \
	"	mat4 matrix1; //inverted modelview matrix\n" \
	"} base;\n" \
	"\n" \
	"layout(binding = 2) uniform sampler3D tex0;//data volume\n" \
	"layout(binding = 3) uniform sampler3D tex1;//gm volume\n" \
	"\n" \
	"// VOL_UNIFORMS_BRICK\n" \
	"layout(push_constant) uniform VolFragShaderBrickConst {\n" \
	"	vec4 brkscale;//tex transform for bricking and 1/nx\n" \
	"	vec4 brktrans;//tex transform for bricking and 1/ny\n" \
	"	vec4 mskbrkscale;//tex transform for mask bricks and 1/nz\n" \
	"	vec4 mskbrktrans;//tex transform for mask bricks\n" \
	"	vec4 tbmin;//tex bbox min\n" \
	"	vec4 tbmax;//tex bbox max\n" \
	"	vec3 loc4;//(zmin, zmax, dz)\n" \
	"	uint stnum;\n" \
	"} brk;\n" \
	"\n" \
	"//VOL_CLIP_FUNC\n" \
	"bool vol_clip_func(vec4 t)\n" \
	"{\n" \
	"	vec3 brickt = (t.xyz * brk.brkscale.xyz + brk.brktrans.xyz);\n" \
	"	if (dot(brickt.xyz, base.loc10.xyz) + base.loc10.w < 0.0 ||\n" \
	"		dot(brickt.xyz, base.loc11.xyz) + base.loc11.w < 0.0 ||\n" \
	"		dot(brickt.xyz, base.loc12.xyz) + base.loc12.w < 0.0 ||\n" \
	"		dot(brickt.xyz, base.loc13.xyz) + base.loc13.w < 0.0 ||\n" \
	"		dot(brickt.xyz, base.loc14.xyz) + base.loc14.w < 0.0 ||\n" \
	"		dot(brickt.xyz, base.loc15.xyz) + base.loc15.w < 0.0)\n" \
	"		return true;\n" \
	"	else\n" \
	"		return false;\n" \
	"}\n" \
	"\n" \
	"//VOL_HEAD\n" \
	"void main()\n" \
	"{\n" \
	"	vec4 view = vec4(0.0, 0.0, brk.loc4.x, 0.0);\n" \
	"	const vec4 ray = normalize(base.matrix1 * vec4(0.0, 0.0, 1.0, 0.0) / vec4(brk.brkscale.xyz, 1.));\n" \
	"	const vec4 st = base.matrix1 * vec4((base.matrix0 * OutVertex).xy, brk.loc4.x, 1.0) / vec4(brk.brkscale.xyz, 1.) - vec4(brk.brktrans.xyz / brk.brkscale.xyz, 0.0);\n" \
	"	const float step = brk.loc4.z;\n" \
	"	const float invstnum = 1.0 / float(brk.stnum);\n" \
	"	vec4 outcol = vec4(0.0);\n" \
	"	vec4 dir = vec4(brk.brkscale.w, brk.brktrans.w, brk.mskbrkscale.w, 0.0);\n" \
	"\n" \
	"	vec4 TexCoord = st + ray*step*float(0);\n" \
	"	vec4 t = vec4(TexCoord.xyz, 1.0);\n" \
	"	FragColor = t;\n" \
	"\n" \
	"	//VOL_TAIL\n" \
	"}\n" 

VRayShader::VRayShader(
	VkDevice device, bool poly, int channels,
	bool shading, bool fog,
	int peel, bool clip,
	bool hiqual, int mask,
	int color_mode, int colormap, int colormap_proj,
	bool solid, int vertex_shader, int mask_hide_mode, bool persp, int blend_mode, int multi_mode, bool na_mode, bool highlight)
	: device_(device),
	poly_(poly),
	channels_(channels),
	shading_(shading),
	fog_(fog),
	peel_(peel),
	clip_(clip),
	hiqual_(hiqual),
	mask_(mask),
	color_mode_(color_mode),
	colormap_(colormap),
	colormap_proj_(colormap_proj),
	solid_(solid),
	vertex_type_(vertex_shader),
	mask_hide_mode_(mask_hide_mode),
	persp_(persp),
	blend_mode_(blend_mode),
	program_(0),
	multi_mode_(0),
    na_mode_(na_mode),
    highlight_(highlight)
	{
	}

	VRayShader::~VRayShader()
	{
		delete program_;
	}

	bool VRayShader::create()
	{
		string fs,vs;
		if (emit_f(fs)) return true;
		if (emit_v(vs)) return true;
		program_ = new ShaderProgram(vs,fs);
		program_->create(device_);
		return false;
	}

	bool VRayShader::emit_v(string& s)
	{
		ostringstream z;
		z << ShaderProgram::glsl_version_;
		
		z << VRAY_VTX_SHADER_CODE_CORE_PROFILE;

		s = z.str();
		return false;
	}

	string VRayShader::get_colormap_code()
	{
		switch (colormap_)
		{
		case 0:
			return string(VRAY_COLORMAP_CALC0);
		case 1:
			return string(VRAY_COLORMAP_CALC1);
		case 2:
			return string(VRAY_COLORMAP_CALC2);
		case 3:
			return string(VRAY_COLORMAP_CALC3);
		case 4:
			return string(VRAY_COLORMAP_CALC4);
		}
		return string(VRAY_COLORMAP_CALC0);
	}

	string VRayShader::get_colormap_proj()
	{
		switch (colormap_proj_)
		{
		case 0:
			return string(VRAY_TRANSFER_FUNCTION_COLORMAP_VALU0);
		case 1:
			return string(VRAY_TRANSFER_FUNCTION_COLORMAP_VALU1);
		case 2:
			return string(VRAY_TRANSFER_FUNCTION_COLORMAP_VALU2);
		case 3:
			return string(VRAY_TRANSFER_FUNCTION_COLORMAP_VALU3);
		}
		return string(VRAY_TRANSFER_FUNCTION_COLORMAP_VALU0);
	}

	bool VRayShader::emit_f(string& s)
	{
		ostringstream z;

		/*z << VRAY_FRG_SHADER_CODE_TEST_PERSP;
		s = z.str();
		return false;*/

		//version info
		z << ShaderProgram::glsl_version_;
		z << VRAY_FRG_HEADER;

		if (na_mode_)
		{
			z << VRAY_UNIFORMS_INDEX_COLOR;
			z << VRAY_UNIFORMS_LABEL;
		}

		//color modes
		switch (color_mode_)
		{
		case 2://depth map
			z << VRAY_UNIFORMS_DEPTHMAP;
			break;
		case 3://index color
			if (!na_mode_)
				z << VRAY_UNIFORMS_INDEX_COLOR;
			break;
        case 255://index color (depth mode)
            z << VRAY_UNIFORMS_INDEX_COLOR_D;
            break;
                
		}

		// add uniform for depth peeling
		if (peel_ != 0)
			z << VRAY_UNIFORMS_DP;

		// add uniforms for masking
		switch (mask_)
		{
		case 1:
		case 2:
			z << VRAY_UNIFORMS_MASK;
			break;
		case 3:
			if (!na_mode_)
				z << VRAY_UNIFORMS_LABEL;
			break;
		case 4:
			z << VRAY_UNIFORMS_MASK;
			z << VRAY_UNIFORMS_LABEL;
			break;
		}

		if ( (mask_ <= 0 || mask_ == 3)  && mask_hide_mode_ > 0)
			z << VRAY_UNIFORMS_MASK;

		//functions
		if (clip_)
			z << VRAY_CLIP_FUNC;
		
		//the common head
		if (persp_)
		{
			if (color_mode_ == 2)
				z << VRAY_HEAD_PERSP_DMAP;
			else
				z << VRAY_HEAD_PERSP;

			if (mask_hide_mode_ > 0)
				z << VRAY_MASK_RAY_PERSP;
		}
		else
		{
			if (color_mode_ == 2)
				z << VRAY_HEAD_ORTHO_DMAP;
			else
				z << VRAY_HEAD_ORTHO;

			if (mask_hide_mode_ > 0)
				z << VRAY_MASK_RAY_ORTHO;
		}
		if (peel_ != 0)
		{
			if (persp_)
				z << VRAY_HEAD_CLIP_COORD_PERSP;
			else
				z << VRAY_HEAD_CLIP_COORD_ORTHO;
		}
			
		// Set up light variables and input parameters.
		z << VRAY_HEAD_LIT;

		// Set up fog variables and input parameters.
		if (fog_)
			z << VRAY_HEAD_FOG;

		if (color_mode_ == 3 || color_mode_ == 255)
			z << VRAY_HEAD_ID_INITIALIZE;

		if (blend_mode_ == 2)
			z << VRAY_HEAD_MIP;

		if (na_mode_)
			z << VRAY_HEAD_LABEL_SEG;
        
        if (highlight_)
            z << VRAY_HEAD_HIGHLIGHT;

		z << VRAY_LOOP_HEAD;
		if (peel_ != 0 || color_mode_ == 2)
			z << VRAY_LOOP_CLIP_COORD;
		if (peel_ == 1)//draw volume before 15
			z << VRAY_DP_Z1_FAR;
		else if (peel_ == 2 || peel_ == 5)//draw volume after 14
			z << VRAY_DP_Z2_NEAR;
		else if (peel_ == 3 || peel_ == 4)//draw volume after 14 and before 15
			z << VRAY_DP_Z1_FAR << VRAY_DP_Z2_NEAR;

		if (mask_hide_mode_ > 0)
			z << VRAY_MASK_VAL;

		z << VRAY_LOOP_CONDITION_START;

		if (peel_ == 1)//draw volume before 15
			z << VRAY_LOOP_CONDITION_DP_1;
		else if (peel_ == 2 || peel_ == 5)//draw volume after 14
			z << VRAY_LOOP_CONDITION_DP_2;
		else if (peel_ == 3 || peel_ == 4)//draw volume after 14 and before 15
			z << VRAY_LOOP_CONDITION_DP_3;

		if (mask_hide_mode_ == 1)
			z << VRAY_LOOP_CONDITION_HIDE_OUTSIDE_MASK;
		else if (mask_hide_mode_ == 2)
			z << VRAY_LOOP_CONDITION_HIDE_INSIDE_MASK;

		if (color_mode_ != 2)
			z << VRAY_LOOP_CONDITION_END;
		else
			z << VRAY_LOOP_CONDITION_END_DMAP;
        
        if (highlight_)
            z << VRAY_HIGHLIGHT_THRESHOLD;
        
		//bodies
		if (color_mode_ == 3)
		{
			if (shading_)
				z << VRAY_INDEX_COLOR_BODY_SHADE;
			else
				z << VRAY_INDEX_COLOR_BODY;
		}
        else if (color_mode_ == 255)
        {
            z << VRAY_INDEX_COLOR_D_BODY;
        }
		else if (shading_)
		{
			//no gradient volume, need to calculate in real-time
			z << VRAY_DATA_VOLUME_LOOKUP;
            
            if (na_mode_)
                z << VRAY_DATA_LABEL_SEG_IF;

			if (hiqual_)
				z << VRAY_GRAD_COMPUTE_HI;
			else
				z << VRAY_GRAD_COMPUTE_LO;

			z << VRAY_BODY_SHADING;

			if (channels_ == 1)
				z << VRAY_COMPUTED_GM_LOOKUP;
			else
				z << VRAY_TEXTURE_GM_LOOKUP;

			switch (color_mode_)
			{
			case 0://normal
				if (solid_)
					z << VRAY_TRANSFER_FUNCTION_SIN_COLOR_SOLID;
				else
					z << VRAY_TRANSFER_FUNCTION_SIN_COLOR;
				break;
			case 1://colormap
				if (solid_)
				{
					z << VRAY_TRANSFER_FUNCTION_COLORMAP_SOLID;
					z << get_colormap_proj();
					z << get_colormap_code();
					z << VRAY_COMMON_TRANSFER_FUNCTION_CALC;
					z << VRAY_TRANSFER_FUNCTION_COLORMAP_SOLID_RESULT;
				}
				else
				{
					z << VRAY_TRANSFER_FUNCTION_COLORMAP;
					z << get_colormap_proj();
					z << get_colormap_code();
					z << VRAY_COMMON_TRANSFER_FUNCTION_CALC;
					z << VRAY_TRANSFER_FUNCTION_COLORMAP_RESULT;
				}
				break;
			}

			z << VRAY_COLOR_OUTPUT;
		}
		else // No shading
		{
			z << VRAY_DATA_VOLUME_LOOKUP;
            
            if (na_mode_)
                z << VRAY_DATA_LABEL_SEG_IF;

			if (channels_ == 1)
			{
				// Compute Gradient magnitude and use it.
				if (hiqual_)
					z << VRAY_GRAD_COMPUTE_HI;
				else
					z << VRAY_GRAD_COMPUTE_LO;

				z << VRAY_COMPUTED_GM_LOOKUP;
			}
			else
			{
				z << VRAY_TEXTURE_GM_LOOKUP;
			}

			switch (color_mode_)
			{
			case 0://normal
				if (solid_)
					z << VRAY_TRANSFER_FUNCTION_SIN_COLOR_SOLID;
				else
					z << VRAY_TRANSFER_FUNCTION_SIN_COLOR;
				break;
			case 1://colormap
				if (solid_)
				{
					z << VRAY_TRANSFER_FUNCTION_COLORMAP_SOLID;
					z << get_colormap_proj();
					z << get_colormap_code();
					z << VRAY_COMMON_TRANSFER_FUNCTION_CALC;
					z << VRAY_TRANSFER_FUNCTION_COLORMAP_SOLID_RESULT;
				}
				else
				{
					z << VRAY_TRANSFER_FUNCTION_COLORMAP;
					z << get_colormap_proj();
					z << get_colormap_code();
					z << VRAY_COMMON_TRANSFER_FUNCTION_CALC;
					z << VRAY_TRANSFER_FUNCTION_COLORMAP_RESULT;
				}
				break;
			case 2://depth map
				z << VRAY_TRANSFER_FUNCTION_DEPTHMAP;
				break;
			}
		}

		// fog
		if (fog_)
		{
			z << VRAY_FOG_BODY;
		}

		//final blend
		switch (mask_)
		{
		case 0:
			if (color_mode_ == 3 || color_mode_ == 255)
				z << VRAY_RASTER_BLEND_ID;
			else if (color_mode_ == 2)
				z << VRAY_RASTER_BLEND_DMAP;
			else
			{
				if (solid_)
					z << VRAY_RASTER_BLEND_SOLID;
				else
					z << VRAY_RASTER_BLEND;
			}
			break;
		case 1:
			if (color_mode_ == 3 || color_mode_ == 255)
				z << VRAY_RASTER_BLEND_MASK_ID;
			else if (color_mode_ == 2)
				z << VRAY_RASTER_BLEND_MASK_DMAP;
			else
			{
				if (solid_)
					z << VRAY_RASTER_BLEND_MASK_SOLID;
				else
					z << VRAY_RASTER_BLEND_MASK;
			}
			break;
		case 2:
			if (color_mode_ == 3 || color_mode_ == 255)
				z << VRAY_RASTER_BLEND_NOMASK_ID;
			else if (color_mode_ == 2)
				z << VRAY_RASTER_BLEND_NOMASK_DMAP;
			else
			{
				if (solid_)
					z << VRAY_RASTER_BLEND_NOMASK_SOLID;
				else
					z << VRAY_RASTER_BLEND_NOMASK;
			}
			break;
		case 3:
			z << VRAY_RASTER_BLEND_LABEL;
			break;
		case 4:
			z << VRAY_RASTER_BLEND_LABEL_MASK;
			break;
		}

		if (color_mode_ == 2)
			z << VRAY_BLEND_DMAP;
		else if (blend_mode_ == 2)
			z << VRAY_BLEND_MIP;
		else
			z << VRAY_BLEND_OVER;
        
        if (na_mode_)
            z << VRAY_DATA_LABEL_SEG_ENDIF;

        z << VRAY_LOOP_TAIL;
        
        if (highlight_)
            z << VRAY_HIGHLIGHT_ADD;
        
		if (na_mode_ && mask_ != 1)
			z << VRAY_ASSIGN_COLOR_LABEL_SEG;
        else if (na_mode_ && mask_ == 1)
            z << VRAY_ASSIGN_COLOR_NA_MASK;
		else
			z << VRAY_VRAY_ASSIGN_COLOR;

		//the common tail
		z << VRAY_TAIL;

		//output
		s = z.str();
		return false;
	}

	VRayShaderFactory::VRayShaderFactory()
		: prev_shader_(-1)
	{

	}

	VRayShaderFactory::VRayShaderFactory(std::vector<vks::VulkanDevice*> &devices)
		: prev_shader_(-1)
	{
		init(devices);
	}

	void VRayShaderFactory::init(std::vector<vks::VulkanDevice*> &devices)
	{
		vdevices_ = devices;
		setupDescriptorSetLayout();
	}

	VRayShaderFactory::~VRayShaderFactory()
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

	ShaderProgram* VRayShaderFactory::shader(
		VkDevice device,
		bool poly, int channels,
		bool shading, bool fog,
		int peel, bool clip,
		bool hiqual, int mask,
		int color_mode, int colormap, int colormap_proj,
		bool solid, int vertex_shader, int mask_hide_mode, bool persp, int blend_mode, int multi_mode, bool na_mode, bool highlight)
	{
		VRayShader*ret = nullptr;
		if(prev_shader_ >= 0)
		{
			if(shader_[prev_shader_]->match(
				device, poly, channels,
				shading, fog,
				peel, clip,
				hiqual, mask,
				color_mode, colormap, colormap_proj,
				solid, vertex_shader, mask_hide_mode, persp, blend_mode, multi_mode, na_mode, highlight))
			{
				ret = shader_[prev_shader_];
			}
		}
		if (!ret)
		{
			for(unsigned int i=0; i<shader_.size(); i++)
			{
				if(shader_[i]->match(
					device, poly, channels,
					shading, fog,
					peel, clip,
					hiqual, mask,
					color_mode, colormap, colormap_proj,
					solid, vertex_shader, mask_hide_mode, persp, blend_mode, multi_mode, na_mode, highlight))
				{
					prev_shader_ = i;
					ret = shader_[i];
					break;
				}
			}
		}

		if (!ret)
		{
			VRayShader* s = new VRayShader(device, poly, channels,
				shading, fog,
				peel, clip,
				hiqual, mask,
				color_mode, colormap, colormap_proj,
				solid, vertex_shader, mask_hide_mode, persp, blend_mode, multi_mode, na_mode, highlight);
			if(s->create())
				delete s;
			else
			{
				shader_.push_back(s);
				prev_shader_ = int(shader_.size())-1;
				ret = s;
			}
		}

		return ret ? ret->program() : NULL;
	}

	void VRayShaderFactory::setupDescriptorSetLayout()
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
			for (int i = 0; i < ShaderProgram::MAX_SHADER_UNIFORMS; i++)
			{
				setLayoutBindings.push_back(
					vks::initializers::descriptorSetLayoutBinding(
					VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 
					VK_SHADER_STAGE_FRAGMENT_BIT, 
					offset+i)
					);
			}
			
			VkDescriptorSetLayoutCreateInfo descriptorLayout = 
				vks::initializers::descriptorSetLayoutCreateInfo(
				setLayoutBindings.data(),
				static_cast<uint32_t>(setLayoutBindings.size()));

			descriptorLayout.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
			descriptorLayout.pNext = nullptr;

			VRayPipelineSettings pipe;
			VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &pipe.descriptorSetLayout));

			VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
				vks::initializers::pipelineLayoutCreateInfo(
				&pipe.descriptorSetLayout,
				1);

			VkPushConstantRange pushConstantRange = {};
			pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT;
			pushConstantRange.size = sizeof(VRayShaderFactory::VRayFragShaderBrickConst);
			pushConstantRange.offset = 0;

			// Push constant ranges are part of the pipeline layout
			pPipelineLayoutCreateInfo.pushConstantRangeCount = 1;
			pPipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;

			VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &pipe.pipelineLayout));

			pipeline_[vdev] = pipe;
		}
	}
	
	void VRayShaderFactory::getDescriptorSetWriteUniforms(vks::VulkanDevice *vdev, VRayUniformBufs& uniformBuffers, std::vector<VkWriteDescriptorSet>& writeDescriptorSets)
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
				&uniformBuffers.frag_base.descriptor)
		);
	}

	void VRayShaderFactory::getDescriptorSetWriteUniforms(vks::VulkanDevice* vdev, vks::Buffer& vert, vks::Buffer& frag, std::vector<VkWriteDescriptorSet>& writeDescriptorSets)
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
	void VRayShaderFactory::prepareUniformBuffers(std::map<vks::VulkanDevice*, VRayUniformBufs>& uniformBuffers)
	{
		for (auto vulkanDev : vdevices_)
		{
			VRayUniformBufs uniformbufs;

			VK_CHECK_RESULT(vulkanDev->createBuffer(
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
				&uniformbufs.vert,
				sizeof(VRayVertShaderUBO)));

			VK_CHECK_RESULT(vulkanDev->createBuffer(
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
				&uniformbufs.frag_base,
				sizeof(VRayFragShaderBaseUBO)));

			// Map persistent
			VK_CHECK_RESULT(uniformbufs.vert.map());
			VK_CHECK_RESULT(uniformbufs.frag_base.map());

			uniformBuffers[vulkanDev] = uniformbufs;
		}
	}

	void VRayShaderFactory::updateUniformBuffers(VRayUniformBufs& uniformBuffers, VRayVertShaderUBO vubo, VRayFragShaderBaseUBO fubo)
	{
		memcpy(uniformBuffers.vert.mapped, &vubo, sizeof(vubo));
		memcpy(uniformBuffers.frag_base.mapped, &fubo, sizeof(fubo));
	}

	
} // end namespace FLIVR
