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
#include <FLIVR/SegShader.h>
#include <FLIVR/ShaderProgram.h>
#include <FLIVR/VolShaderCode.h>

using std::string;
using std::vector;
using std::ostringstream;

namespace FLIVR
{
#define SEG_INPUTS \
	"#pragma optionNV(inline all)\n" \
	"#pragma optionNV(fastmath on)\n" \
	"#pragma optionNV(ifcvt none)\n" \
	"#pragma optionNV(strict on)\n" \
	"#pragma optionNV(unroll all)\n" \
	"layout (local_size_x = 4, local_size_y = 4, local_size_z = 4) in;\n" \

#define SEG_HEAD \
	"//SEG_HEAD\n" \
	"void main()\n" \
	"{\n" \
	"	vec4 TexCoord = vec4((gl_GlobalInvocationID.x+0.5)*brk.loc4.x, (gl_GlobalInvocationID.y+0.5)*brk.loc4.y, (gl_GlobalInvocationID.z+0.5)*brk.loc4.z, 1.0);\n" \
	"	vec4 t = TexCoord;\n" \
	"\n"

#define SEG_HEAD_CLIP_FUNC \
	"	if (vol_clip_func(t))\n" \
	"	{\n" \
	"		return;\n" \
	"	}\n" \
	"\n"

#define SEG_HEAD_LIT \
	"	//SEG_HEAD_LIT\n" \
	"	vec4 l = base.loc0; // {lx, ly, lz, alpha}\n" \
	"	vec4 k = base.loc1; // {ka, kd, ks, ns}\n" \
	"	k.x = k.x>1.0?log2(3.0-k.x):k.x;\n" \
	"	vec4 n, w;\n" \
	"	if (l.w == 0.0) { return; }\n" \
	"\n"

#define SEG_MASK_INOUT \
	"//SEG_MASK_INOUT\n" \
	"layout (binding = 0, r8) uniform image3D maskimg;\n"

#define SEG_MASK_16BIT_INOUT \
	"//SEG_MASK_16BIT_INOUT\n" \
	"layout (binding = 0, r16) uniform image3D maskimg;\n"

#define SEG_LABEL_INOUT \
	"//SEG_LABEL_INOUT\n" \
	"layout (binding = 9, r32ui) uniform image3D labelimg;\n"

#define SEG_PAINT_INOUT \
	"//SEG_PAINT_INOUT\n" \
	"layout (binding = 10, r8) uniform image3D strokeimg;\n"

#define SEG_UNIFORMS_BASE \
	"// SEG_UNIFORMS_BASE\n" \
	"layout (binding = 1) uniform SegCompShaderBaseUBO {\n" \
	"	vec4 loc0;//(lx, ly, lz, alpha)\n" \
	"	vec4 loc1;//(ka, kd, ks, ns)\n" \
	"	vec4 loc2;//(scalar_scale, gm_scale, left_thresh, right_thresh)\n" \
	"	vec4 loc3;//(gamma, gm_thresh, offset, sw)\n" \
	"	vec4 loc5;//(spcx, spcy, spcz, max_id)\n" \
	"	vec4 loc6;//(r, g, b, 0.0) or (1/vx, 1/vy, luminance, depth_mode)\n" \
	"	vec4 loc7;//(ini_thresh, gm_falloff, scl_falloff, scl_translate)\n" \
	"	vec4 loc8;//(w2d, bins, use_absolute_value, 0.0)\n" \
	"	vec4 loc10; //plane0\n" \
	"	vec4 loc11; //plane1\n" \
	"	vec4 loc12; //plane2\n" \
	"	vec4 loc13; //plane3\n" \
	"	vec4 loc14; //plane4\n" \
	"	vec4 loc15; //plane5\n" \
	"	mat4 matrix0; //modelview matrix\n" \
	"	mat4 matrix1; //projection matrix\n" \
	"	mat4 matrix3;//modelview matrix inverse\n" \
	"	mat4 matrix4;//projection matrix inverse\n" \
	"} base;\n" \
	"\n" \
	"layout (binding = 2) uniform sampler3D tex0;//data volume\n" \
	"\n" \

#define SEG_UNIFORMS_BRICK \
	"// SEG_UNIFORMS_BRICK\n" \
	"layout (push_constant) uniform SegCompShaderBrickConst {\n" \
	"	vec4 loc4;//(1/nx, 1/ny, 1/nz, 1/sample_rate)\n" \
	"	vec4 loc9;//(nx, ny, nz, 0)\n" \
	"	vec4 brkscale;//tex transform for bricking\n" \
	"	vec4 brktrans;//tex transform for bricking\n" \
	"	vec4 mskbrkscale;//tex transform for mask bricks\n" \
	"	vec4 mskbrktrans;//tex transform for mask bricks\n" \
	"} brk;\n" \
	"\n"

#define SEG_UNIFORMS_WMAP_2D \
	"//SEG_UNIFORMS_WMAP_2D\n" \
	"layout(binding = 6) uniform sampler2D tex4;//2d weight map (after tone mapping)\n" \
	"layout(binding = 7) uniform sampler2D tex5;//2d weight map (before tone mapping)\n" \
	"\n"

#define SEG_UNIFORMS_MASK_2D \
	"//SEG_UNIFORMS_MASK_2D\n" \
	"layout(binding = 8) uniform sampler2D tex6;//2d mask\n" \
	"\n"

#define SEG_TAIL \
	"//SEG_TAIL\n" \
	"}\n"

#define SEG_BODY_STROKE_CLEAR \
	"	//SEG_BODY_STROKE_CLEAR\n" \
	"	imageStore(strokeimg, ivec3(gl_GlobalInvocationID.xyz), vec4(0.0));\n" \
	"\n"

#define SEG_BODY_BOUND_CHECK \
	"	//SEG_BODY_BOUND_CHECK\n" \
	"	if (gl_GlobalInvocationID.x >= brk.loc9.x || gl_GlobalInvocationID.y >= brk.loc9.y || gl_GlobalInvocationID.z >= brk.loc9.z)\n"\
	"	{\n"\
	"		return;\n"\
	"	}\n"\
	"\n"

#define SEG_BODY_WEIGHT \
	"	//SEG_BODY_WEIGHT\n" \
	"	vec4 cw2d;\n" \
	"	vec4 weight1 = texture(tex4, s.xy);\n" \
	"	vec4 weight2 = texture(tex5, s.xy);\n" \
	"	cw2d.x = weight2.x==0.0?0.0:weight1.x/weight2.x;\n" \
	"	cw2d.y = weight2.y==0.0?0.0:weight1.y/weight2.y;\n" \
	"	cw2d.z = weight2.z==0.0?0.0:weight1.z/weight2.z;\n" \
	"	float weight2d = max(cw2d.x, max(cw2d.y, cw2d.z));\n" \
	"\n"

#define SEG_BODY_BLEND_WEIGHT \
	"	//SEG_BODY_BLEND_WEIGHT\n" \
	"	c = base.loc8.x>1.0?c*base.loc8.x*weight2d:(1.0-base.loc8.x)*c+base.loc8.x*c*weight2d;\n" \
	"\n"

#define SEG_BODY_INIT_CLEAR \
	"	//SEG_BODY_INIT_CLEAR\n" \
	"	imageStore(maskimg, ivec3(gl_GlobalInvocationID.xyz), vec4(0.0));\n" \
	"\n"

#define SEG_BODY_INIT_2D_COORD \
	"	//SEG_BODY_INIT_2D_COORD\n" \
	"	vec4 s = base.matrix1 * base.matrix0 * (t*brk.brkscale + brk.brktrans);\n"\
	"	s = s / s.w;\n"\
	"	s.xy = s.xy / 2.0 + 0.5;\n"\
	"\n"

#define SEG_BODY_INIT_CULL \
	"	//SEG_BODY_INIT_CULL\n" \
	"	float cmask2d = texture(tex6, s.xy).x;\n"\
	"	if (cmask2d < 0.95)\n"\
	"		return;\n"\
	"\n"

#define SEG_BODY_INIT_CULL_ERASE \
	"	//SEG_BODY_INIT_CULL_ERASE\n" \
	"	float cmask2d = texture(tex6, s.xy).x;\n"\
	"	if (cmask2d < 0.45)\n"\
	"	{\n"\
	"		return;\n"\
	"	}\n"\
	"\n"

#define SEG_BODY_INIT_BLEND_HEAD \
	"	//SEG_BODY_INIT_BLEND_HEAD\n" \
	"	c = c*l.w;\n" \
	"\n"

#define SEG_BODY_INIT_BLEND_APPEND \
	"	//SEG_BODY_INIT_BLEND_APPEND\n" \
	"	vec4 ret = vec4(c.x>0.0?(c.x>base.loc7.x?1.0:0.0):0.0);\n" \
	"	imageStore(maskimg, ivec3(gl_GlobalInvocationID.xyz), ret);\n" \
	"\n"

#define SEG_BODY_INIT_BLEND_APPEND_STROKE_TAIL \
	"	//SEG_BODY_INIT_BLEND_APPEND_STROKE_TAIL\n" \
	"	imageStore(strokeimg, ivec3(gl_GlobalInvocationID.xyz), vec4(1.0));\n" \
	"\n"

#define SEG_BODY_INIT_BLEND_ERASE \
	"	//SEG_BODY_INIT_BLEND_ERASE\n" \
	"	imageStore(maskimg, ivec3(gl_GlobalInvocationID.xyz), vec4(0.0));\n" \
	"\n"

#define SEG_BODY_INIT_BLEND_ERASE_STROKE_TAIL \
	"	//SEG_BODY_INIT_BLEND_ERASE_STROKE_TAIL\n" \
	"	imageStore(strokeimg, ivec3(gl_GlobalInvocationID.xyz), vec4(0.0));\n" \
	"\n"

#define SEG_BODY_INIT_BLEND_DIFFUSE \
	"	//SEG_BODY_INIT_BLEND_DIFFUSE\n" \
	"	vec4 ret = texture(tex2, t.stp);\n" \
	"	imageStore(maskimg, ivec3(gl_GlobalInvocationID.xyz), ret);\n" \
	"\n"

#define SEG_BODY_INIT_BLEND_DIFFUSE_STROKE_TAIL \
	"	//SEG_BODY_INIT_BLEND_DIFFUSE_STROKE_TAIL\n" \
	"	imageStore(strokeimg, ivec3(gl_GlobalInvocationID.xyz), ret);\n" \
	"\n"

#define SEG_BODY_INIT_BLEND_FLOOD \
	"	//SEG_BODY_INIT_BLEND_FLOOD\n" \
	"	vec4 ret = vec4(c.x>0.0?(c.x>base.loc7.x?1.0:0.0):0.0);\n" \
	"	imageStore(maskimg, ivec3(gl_GlobalInvocationID.xyz), ret);\n" \
	"\n"

#define SEG_BODY_INIT_BLEND_FLOOD_STROKE_TAIL \
	"	//SEG_BODY_INIT_BLEND_FLOOD_STROKE_TAIL\n" \
	"	imageStore(strokeimg, ivec3(gl_GlobalInvocationID.xyz), vec4(1.0));\n" \
	"\n"

#define SEG_BODY_INIT_BLEND_ALL \
	"	//SEG_BODY_INIT_BLEND_ALL\n" \
	"	imageStore(maskimg, ivec3(gl_GlobalInvocationID.xyz), vec4(1.0));\n" \
	"\n"

#define SEG_BODY_INIT_BLEND_ALL_STROKE_TAIL \
	"	//SEG_BODY_INIT_BLEND_ALL_STROKE_TAIL\n" \
	"	imageStore(strokeimg, ivec3(gl_GlobalInvocationID.xyz), vec4(1.0));\n" \
	"\n"

#define SEG_BODY_INIT_BLEND_HR_ORTHO_STROKE_HEAD \
	"	//SEG_BODY_INIT_BLEND_HR_ORTHO_STROKE_HEAD\n" \
	"	imageStore(strokeimg, ivec3(gl_GlobalInvocationID.xyz), vec4(1.0));\n" \
	"\n"

#define SEG_BODY_INIT_BLEND_HR_ORTHO \
	"	//SEG_BODY_INIT_BLEND_HR_ORTHO\n" \
	"	if (c.x <= base.loc7.x)\n" \
	"	{\n" \
	"		imageStore(maskimg, ivec3(gl_GlobalInvocationID.xyz), vec4(0.0));\n" \
	"		return;\n" \
	"	}\n" \
	"	vec4 cv = base.matrix3 * vec4(0.0, 0.0, 1.0, 0.0) / vec4(brk.brkscale.xyz, 1.);\n" \
	"	vec3 step = cv.xyz;\n" \
	"	step = normalize(step);\n" \
	"	step = step * length(step * brk.loc4.xyz);\n" \
	"	vec3 ray = t.xyz;\n" \
	"	vec4 cray;\n" \
	"	bool flag = false;\n" \
	"	while (true)\n" \
	"	{\n" \
	"		ray += step;\n" \
	"		if (any(greaterThan(ray, vec3(1.0))) ||\n" \
	"				any(lessThan(ray, vec3(0.0))))\n" \
	"			break;\n" \
	"		if (vol_clip_func(vec4(ray, 1.0)))\n" \
	"			break;\n" \
	"		v.x = texture(tex0, ray).x;\n" \
	"		cray.x = base.loc8.z > 0.5 ? v.x : (base.loc2.x<0.0?(1.0+v.x*base.loc2.x):v.x*base.loc2.x);\n" \
	"		if (cray.x > base.loc7.x && flag)\n" \
	"		{\n" \
	"			imageStore(maskimg, ivec3(gl_GlobalInvocationID.xyz), vec4(0.0));\n" \
	"			return;\n" \
	"		}\n" \
	"		if (cray.x <= base.loc7.x)\n" \
	"			flag = true;\n" \
	"	}\n" \
	"	imageStore(maskimg, ivec3(gl_GlobalInvocationID.xyz), vec4(1.0));\n" \
	"\n"

#define SEG_BODY_INIT_BLEND_HR_PERSP_STROKE_HEAD \
	"	//SEG_BODY_INIT_BLEND_HR_PERSP_STROKE_HEAD\n" \
	"	imageStore(strokeimg, ivec3(gl_GlobalInvocationID.xyz), vec4(1.0));\n" \
	"\n"

#define SEG_BODY_INIT_BLEND_HR_PERSP \
	"	//SEG_BODY_INIT_BLEND_HR_PERSP\n" \
	"	if (c.x <= base.loc7.x)\n" \
	"	{\n" \
	"		imageStore(maskimg, ivec3(gl_GlobalInvocationID.xyz), vec4(0.0));\n" \
	"		return;\n" \
	"	}\n" \
	"	vec4 cv = base.matrix3 * vec4(0.0, 0.0, 0.0, 1.0) / vec4(brk.brkscale.xyz, 1.);\n" \
	"	cv = cv / cv.w;\n" \
	"	vec3 step = cv.xyz - t.xyz;\n" \
	"	step = normalize(step);\n" \
	"	step = step * length(step * brk.loc4.xyz);\n" \
	"	vec3 ray = t.xyz;\n" \
	"	vec4 cray;\n" \
	"	bool flag = false;\n" \
	"	while (true)\n" \
	"	{\n" \
	"		ray += step;\n" \
	"		if (any(greaterThan(ray, vec3(1.0))) ||\n" \
	"				any(lessThan(ray, vec3(0.0))))\n" \
	"			break;\n" \
	"		if (vol_clip_func(vec4(ray, 1.0)))\n" \
	"			break;\n" \
	"		v.x = texture(tex0, ray).x;\n" \
	"		cray.x = base.loc8.z > 0.5 ? v.x : (base.loc2.x<0.0?(1.0+v.x*base.loc2.x):v.x*base.loc2.x);\n" \
	"		if (cray.x > base.loc7.x && flag)\n" \
	"		{\n" \
	"			imageStore(maskimg, ivec3(gl_GlobalInvocationID.xyz), vec4(0.0));\n" \
	"			return;\n" \
	"		}\n" \
	"		if (cray.x <= base.loc7.x)\n" \
	"			flag = true;\n" \
	"	}\n" \
	"	imageStore(maskimg, ivec3(gl_GlobalInvocationID.xyz), vec4(1.0));\n" \
	"\n"

#define SEG_BODY_INIT_POSTER \
	"	//SEG_BODY_INIT_POSTER\n" \
	"	vec4 ret = vec4(ceil(c.x*base.loc8.y)/base.loc8.y);\n" \
	"	imageStore(maskimg, ivec3(gl_GlobalInvocationID.xyz), ret);\n" \
	"\n"

#define SEG_BODY_INIT_POSTER_STROKE_TAIL \
	"	//SEG_BODY_INIT_POSTER_STROKE_TAIL\n" \
	"	imageStore(strokeimg, ivec3(gl_GlobalInvocationID.xyz), ret);\n" \
	"\n"

#define SEG_BODY_DB_GROW_2D_COORD \
	"	//SEG_BODY_DB_GROW_2D_COORD\n" \
	"	vec4 s = base.matrix1 * base.matrix0 * (t*brk.brkscale + brk.brktrans);\n"\
	"	s = s / s.w;\n"\
	"	s.xy = s.xy / 2.0 + 0.5;\n"\
	"	vec4 cc = texture(tex2, t.stp);\n"\
	"\n"

#define SEG_BODY_DB_GROW_CULL \
	"	//SEG_BODY_DB_GROW_CULL\n" \
	"	float cmask2d = texture(tex6, s.xy).x;\n"\
	"	if (cmask2d < 0.45 /*|| cmask2d > 0.55*/)\n"\
	"		return;\n"\
	"\n"

#define SEG_BODY_DB_GROW_STOP_FUNC \
	"	//SEG_BODY_DB_GROW_STOP_FUNC\n" \
	"	if (c.x == 0.0)\n" \
	"		return;\n" \
	"	v.x = c.x>1.0?1.0:c.x;\n" \
	"	float stop = \n" \
	"		(base.loc7.y>=1.0?1.0:(v.y>sqrt(base.loc7.y)*2.12?0.0:exp(-v.y*v.y/base.loc7.y)))*\n" \
	"		(v.x>base.loc7.w?1.0:(base.loc7.z>0.0?(v.x<base.loc7.w-sqrt(base.loc7.z)*2.12?0.0:exp(-(v.x-base.loc7.w)*(v.x-base.loc7.w)/base.loc7.z)):0.0));\n" \
	"	if (stop == 0.0)\n" \
	"		return;\n" \
	"\n"

#define SEG_BODY_DB_GROW_BLEND_APPEND_HEAD \
	"	//SEG_BODY_DB_GROW_BLEND_APPEND_HEAD\n" \
	"	imageStore(strokeimg, ivec3(gl_GlobalInvocationID.xyz), (1.0-stop) * cc);\n" \
	"\n"

#define SEG_BODY_DB_GROW_BLEND_APPEND \
	"	//SEG_BODY_DB_GROW_BLEND_APPEND\n" \
	"	vec4 result = (1.0-stop) * cc;\n" \
	"	vec3 nb;\n" \
	"	vec3 max_nb = t.stp;\n" \
	"	float m;\n" \
	"	float mx;\n" \
	"	for (int i=-1; i<2; i++)\n"\
	"	for (int j=-1; j<2; j++)\n"\
	"	for (int k=-1; k<2; k++)\n"\
	"	{\n"\
	"		nb = vec3(t.s+float(i)*brk.loc4.x, t.t+float(j)*brk.loc4.y, t.p+float(k)*brk.loc4.z);\n"\
	"		m = texture(tex2, nb).x;\n" \
	"		if (m > cc.x)\n" \
	"		{\n" \
	"			cc = vec4(m);\n" \
	"			max_nb = nb;\n" \
	"		}\n" \
	"	}\n"\
	"	if (base.loc7.y>0.0)\n" \
	"	{\n" \
	"		m = texture(tex0, max_nb).x + base.loc7.y;\n" \
	"		mx = texture(tex0, t.stp).x;\n" \
	"		if (m < mx || m - mx > 2.0*base.loc7.y)\n" \
	"			return;\n" \
	"	}\n" \
	"	result += cc*stop;\n"\
	"	imageStore(maskimg, ivec3(gl_GlobalInvocationID.xyz), result);\n" \
	"\n"

#define SEG_BODY_DB_GROW_BLEND_APPEND_TAIL \
	"	//SEG_BODY_DB_GROW_BLEND_APPEND_HEAD\n" \
	"	imageStore(strokeimg, ivec3(gl_GlobalInvocationID.xyz), result);\n" \
	"\n"

#define SEG_BODY_DB_GROW_BLEND_ERASE0 \
	"	//SEG_BODY_DB_GROW_BLEND_ERASE0\n" \
	"	for (int i=-1; i<2; i++)\n"\
	"	for (int j=-1; j<2; j++)\n"\
	"	for (int k=-1; k<2; k++)\n"\
	"	{\n"\
	"		vec3 nb = vec3(t.s+float(i)*brk.loc4.x, t.t+float(j)*brk.loc4.y, t.p+float(k)*brk.loc4.z);\n"\
	"		cc = vec4(min(cc.x, texture(tex2, nb).x));\n"\
	"	}\n"\
	"	vec4 ret = cc*clamp(1.0-stop, 0.0, 1.0);\n"\
	"	imageStore(maskimg, ivec3(gl_GlobalInvocationID.xyz), ret);\n" \
	"\n"

#define SEG_BODY_DB_GROW_BLEND_ERASE0_STROKE_TAIL \
	"	//SEG_BODY_DB_GROW_BLEND_ERASE0_STROKE_TAIL\n" \
	"	imageStore(strokeimg, ivec3(gl_GlobalInvocationID.xyz), ret);\n" \
	"\n"

#define SEG_BODY_DB_GROW_BLEND_ERASE \
	"	//SEG_BODY_DB_GROW_BLEND_ERASE\n" \
	"	imageStore(maskimg, ivec3(gl_GlobalInvocationID.xyz), vec4(0.0));\n" \
	"\n"

#define SEG_BODY_DB_GROW_BLEND_ERASE_STROKE_TAIL \
	"	//SEG_BODY_DB_GROW_BLEND_ERASE_STROKE_TAIL\n" \
	"	imageStore(strokeimg, ivec3(gl_GlobalInvocationID.xyz), vec4(0.0));\n" \
	"\n"

#define SEG_BODY_LABEL_INITIALIZE \
	"	//SEG_BODY_LABEL_INITIALIZE\n" \
	"	vec4 mask = texture(tex2, t.stp)*c.x;\n" \
	"	if (mask.x == 0.0 || mask.x < base.loc7.x)\n" \
	"	{\n" \
	"		imageStore(labelimg, ivec3(gl_GlobalInvocationID.xyz), uvec4(0));\n" \
	"		return;\n" \
	"	}\n" \
	"\n"

#define SEG_BODY_LABEL_INIT_ORDER \
	"	//SEG_BODY_LABEL_INIT_ORDER\n" \
	"	vec3 int_pos = vec3(t.x*brk.loc9.x, t.y*brk.loc9.y, t.z*brk.loc9.z);\n" \
	"	uint index = uint(int_pos.z*brk.loc9.x*brk.loc9.y+int_pos.y*brk.loc9.x+int_pos.x+1);\n" \
	"	imageStore(labelimg, ivec3(gl_GlobalInvocationID.xyz), uvec4(index));\n" \
	"\n"

#define SEG_BODY_LABEL_INIT_COPY \
	"	//SEG_BODY_LABEL_INIT_COPY\n" \
	"	return;\n" \
	"\n"

#define SEG_BODY_LABEL_INITIALIZE_NOMASK \
	"	//SEG_BODY_LABEL_INITIALIZE_NOMASK\n" \
	"	if (c.x ==0.0 || c.x <= base.loc7.x)\n" \
	"	{\n" \
	"		imageStore(labelimg, ivec3(gl_GlobalInvocationID.xyz), uvec4(0));\n" \
	"		return;\n" \
	"	}\n" \
	"\n"

#define SEG_BODY_LABEL_INIT_POSTER \
	"	//SEG_BODY_LABEL_INIT_POSTER\n" \
	"	vec4 mask = texture(tex2, t.stp);\n" \
	"	if (mask.x < 0.001)\n" \
	"	{\n" \
	"		imageStore(labelimg, ivec3(gl_GlobalInvocationID.xyz), uvec4(0));\n" \
	"		return;\n" \
	"	}\n" \
	"\n"

#define VOL_MEASURE_GM_LOOKUP \
	"	//VOL_MEASURE_GM_LOOKUP\n" \
	"	w = vol_grad_func(t, brk.loc4);\n" \
	"	n.xyz = clamp(normalize(w.xyz), vec3(0.0), vec3(1.0));\n" \
	"	v.y = length(w.xyz);\n" \
	"	v.y = 0.5 * (base.loc2.x<0.0?(1.0+v.y*base.loc2.x):v.y*base.loc2.x);\n" \
	"\n"

#define SEG_BODY_LABEL_MAX_FILTER \
	"	//SEG_BODY_LABEL_MAX_FILTER\n" \
	"	uint int_val = texture(tex3, t.stp).x;\n" \
	"	if (int_val == uint(0))\n" \
	"		return;\n" \
	"	vec3 nb;\n" \
	"	vec3 max_nb = t.stp;\n" \
	"	uint m;\n" \
	"	for (int i=-1; i<2; i++)\n" \
	"	for (int j=-1; j<2; j++)\n" \
	"	for (int k=-1; k<2; k++)\n" \
	"	{\n" \
	"		if (k==0 && (i!=0 || j!=0))\n" \
	"			continue;\n" \
	"		nb = vec3(t.s+float(i)*brk.loc4.x, t.t+float(j)*brk.loc4.y, t.p+float(k)*brk.loc4.z);\n" \
	"		m = texture(tex3, nb).x;\n" \
	"		if (m > int_val)\n" \
	"		{\n" \
	"			int_val = m;\n" \
	"			max_nb = nb;\n" \
	"		}\n" \
	"	}\n" \
	"	if (texture(tex0, max_nb).x+base.loc7.y < texture(tex0, t.stp).x)\n" \
	"		return;\n" \
	"	imageStore(labelimg, ivec3(gl_GlobalInvocationID.xyz), uvec4(int_val));\n" \
	"\n"

#define SEG_BODY_LABEL_MIF_POSTER \
	"	//SEG_BODY_LABEL_MIF_POSTER\n" \
	"	uint int_val = texture(tex3, t.stp).x;\n" \
	"	if (int_val == uint(0))\n" \
	"	{\n" \
	"		imageStore(labelimg, ivec3(gl_GlobalInvocationID.xyz), uvec4(0));\n" \
	"		return;\n" \
	"	}\n" \
	"	float cur_val = texture(tex2, t.stp).x;\n" \
	"	float nb_val;\n" \
	"	for (int i=-1; i<2; i++)\n" \
	"	for (int j=-1; j<2; j++)\n" \
	"	for (int k=-1; k<2; k++)\n" \
	"	{\n" \
	"		vec3 nb = vec3(t.s+float(i)*base.loc0.x, t.t+float(j)*base.loc0.y, t.p+float(k)*base.loc0.z);\n" \
	"		nb_val = texture(tex2, nb).x;\n" \
	"		if (abs(nb_val-cur_val) < 0.001)\n" \
	"			int_val = max(int_val, texture(tex3, nb).x);\n" \
	"	}\n" \
	"	imageStore(labelimg, ivec3(gl_GlobalInvocationID.xyz), uvec4(int_val));\n" \
	"\n"

// max_val starts below every value (-0.1): ">= 0.0" means "a lower
// neighbour was found", including an exact 0. With "> 0.0" noise surrounded
// by zero background (binary / masked / cropped data) was never lowered, so
// Noise Removal changed nothing there.
#define FLT_BODY_NR \
	"	//FLT_BODY_NR\n" \
	"	float vc = texture(tex0, t.stp).x;\n" \
	"	float mc = texture(tex2, t.stp).x;\n" \
	"	if (mc < 0.001)\n" \
	"		return;\n" \
	"	float nb_val;\n" \
	"	float max_val = -0.1;\n" \
	"	for (int i=-2; i<3; i++)\n" \
	"	for (int j=-2; j<3; j++)\n" \
	"	for (int k=-2; k<3; k++)\n" \
	"	{\n" \
	"		vec3 nb = vec3(t.s+float(i)*brk.loc4.x, t.t+float(j)*brk.loc4.y, t.p+float(k)*brk.loc4.z);\n" \
	"		nb_val = texture(tex0, nb).x;\n" \
	"		max_val = (nb_val<vc && nb_val>max_val)?nb_val:max_val;\n" \
	"	}\n" \
	"	if (max_val >= 0.0)\n" \
	"		imageStore(maskimg, ivec3(gl_GlobalInvocationID.xyz), vec4(max_val));\n" \
	"	else\n" \
	"		return;\n" \
	"\n"

	SegShader::SegShader(VkDevice device, int type, int paint_mode, int hr_mode,
		bool use_2d, bool shading, int peel, bool clip, bool hiqual, bool use_stroke, bool stroke_clear, int out_bytes, bool use_abs) :
	device_(device),
	type_(type),
	paint_mode_(paint_mode),
	hr_mode_(hr_mode),
	use_2d_(use_2d),
	shading_(shading),
	peel_(peel),
	clip_(clip),
	hiqual_(hiqual),
	use_stroke_(use_stroke),
	stroke_clear_(stroke_clear),
	out_bytes_(out_bytes),
    use_abs_(use_abs),
	program_(0)
	{}

	SegShader::~SegShader()
	{
		delete program_;
	}

	bool SegShader::create()
	{
		string cs;
		if (emit(cs)) return true;
		program_ = new ShaderProgram(cs);
		program_->create(device_);
		return false;
	}

	bool SegShader::emit(string& s)
	{
		ostringstream z;

		z << ShaderProgram::glsl_version_;
		z << SEG_INPUTS;

		z << SEG_UNIFORMS_BASE;
		z << SEG_UNIFORMS_BRICK;

		//uniforms
		switch (type_)
		{
		case SEG_SHDR_INITIALIZE:
		case SEG_SHDR_DB_GROW:
			if (out_bytes_ == 2) z << SEG_MASK_16BIT_INOUT;
			else z << SEG_MASK_INOUT;
			if (use_stroke_) z << SEG_PAINT_INOUT;
			z << VOL_UNIFORMS_MASK;
			if (use_2d_)
				z << SEG_UNIFORMS_WMAP_2D;
			z << SEG_UNIFORMS_MASK_2D;
			break;
		case LBL_SHDR_INITIALIZE:
			z << SEG_LABEL_INOUT;
			if (use_2d_)
				z << VOL_UNIFORMS_MASK;
			break;
		case LBL_SHDR_MIF:
			z << SEG_LABEL_INOUT;
			z << VOL_UNIFORMS_LABEL;
			if (paint_mode_==1)
				z << VOL_UNIFORMS_MASK;
			break;
		case FLT_SHDR_NR:
			if (out_bytes_ == 2) z << SEG_MASK_16BIT_INOUT;
			else z << SEG_MASK_INOUT;
			z << VOL_UNIFORMS_MASK;
			break;
		}

		//functions
		if (type_==SEG_SHDR_INITIALIZE &&
			(paint_mode_==1 || paint_mode_==2) &&
			hr_mode_>0)
		{
			z << VOL_GRAD_COMPUTE_FUNC;
			z << VOL_TRANSFER_FUNCTION_SIN_COLOR_L_FUNC;
		}
		else if (type_==LBL_SHDR_MIF)
		{
			z << VOL_GRAD_COMPUTE_FUNC;
		}

		if (paint_mode_!=6 && clip_)
			z << VOL_CLIP_FUNC;

		//the common head
		z << SEG_HEAD;

		z << SEG_BODY_BOUND_CHECK;

		if (use_stroke_ && stroke_clear_)
			z << SEG_BODY_STROKE_CLEAR;

		//head for clipping planes
		if (paint_mode_!=6 && clip_)
			z << SEG_HEAD_CLIP_FUNC;

		if (paint_mode_ == 6)
		{
			z << SEG_BODY_INIT_CLEAR;
		}
		else
		{
			//bodies
			switch (type_)
			{
			case SEG_SHDR_INITIALIZE:
				z << SEG_BODY_INIT_2D_COORD;
				if (paint_mode_==1 ||
					paint_mode_==2 ||
					paint_mode_==4 ||
					paint_mode_==8)
					z << SEG_BODY_INIT_CULL;
				else if (paint_mode_==3)
					z << SEG_BODY_INIT_CULL_ERASE;

				if (paint_mode_ != 3)
				{
					z << SEG_HEAD_LIT;
					z << VOL_DATA_VOLUME_LOOKUP;
					//z << VOL_GRAD_COMPUTE_HI;
					//z << VOL_COMPUTED_GM_LOOKUP;
                    if (use_abs_)
                        z << VOL_TRANSFER_FUNCTION_SIN_COLOR_ORG;
                    else
                        z << VOL_TRANSFER_FUNCTION_SIN_COLOR_L;

					if (use_2d_)
					{
						z << SEG_BODY_WEIGHT;
						z << SEG_BODY_BLEND_WEIGHT;
					}
				}

				if (paint_mode_==1 ||
					paint_mode_==2)
				{
					if (hr_mode_ == 0) {
						z << SEG_BODY_INIT_BLEND_APPEND;
						if (use_stroke_) z << SEG_BODY_INIT_BLEND_APPEND_STROKE_TAIL;
					} else if (hr_mode_ == 1) {//ortho
						if (use_stroke_) z << SEG_BODY_INIT_BLEND_HR_ORTHO_STROKE_HEAD;
						z << SEG_BODY_INIT_BLEND_HR_ORTHO;
					} else if (hr_mode_ == 2) {//persp
						if (use_stroke_) z << SEG_BODY_INIT_BLEND_HR_PERSP_STROKE_HEAD;
						z << SEG_BODY_INIT_BLEND_HR_PERSP;
					}
				}
				else if (paint_mode_==3) {
					z << SEG_BODY_INIT_BLEND_ERASE;
					if (use_stroke_) z << SEG_BODY_INIT_BLEND_ERASE_STROKE_TAIL;
				} else if (paint_mode_==4) {
					z << SEG_BODY_INIT_BLEND_DIFFUSE;
					if (use_stroke_) z << SEG_BODY_INIT_BLEND_DIFFUSE_STROKE_TAIL;
				} else if (paint_mode_==5) {
					z << SEG_BODY_INIT_BLEND_FLOOD;
					if (use_stroke_) z << SEG_BODY_INIT_BLEND_FLOOD_STROKE_TAIL;
				} else if (paint_mode_==7) {
					z << SEG_BODY_INIT_BLEND_ALL;
					if (use_stroke_) z << SEG_BODY_INIT_BLEND_ALL_STROKE_TAIL;
				} else if (paint_mode_==8) {
					z << SEG_BODY_INIT_BLEND_ALL;
					if (use_stroke_) z << SEG_BODY_INIT_BLEND_ALL_STROKE_TAIL;
				} else if (paint_mode_==11) {
					z << SEG_BODY_INIT_POSTER;
					if (use_stroke_) z << SEG_BODY_INIT_POSTER_STROKE_TAIL;
				}
				break;
			case SEG_SHDR_DB_GROW:
				z << SEG_BODY_DB_GROW_2D_COORD;
				if (paint_mode_!=5)
					z << SEG_BODY_DB_GROW_CULL;

				if (paint_mode_ != 3)
				{
					z << SEG_HEAD_LIT;
					z << VOL_DATA_VOLUME_LOOKUP;
					z << VOL_GRAD_COMPUTE_HI;
					z << VOL_COMPUTED_GM_LOOKUP;
                    if (use_abs_)
                        z << VOL_TRANSFER_FUNCTION_SIN_COLOR_ORG;
                    else
                        z << VOL_TRANSFER_FUNCTION_SIN_COLOR_L;

					if (use_2d_)
					{
						z << SEG_BODY_WEIGHT;
						z << SEG_BODY_BLEND_WEIGHT;
					}

					z << SEG_BODY_DB_GROW_STOP_FUNC;
				}

				if (paint_mode_==1 ||
					paint_mode_==2 ||
					paint_mode_==4 ||
					paint_mode_==5) {
					if (use_stroke_) z << SEG_BODY_DB_GROW_BLEND_APPEND_HEAD;
					z << SEG_BODY_DB_GROW_BLEND_APPEND;
					if (use_stroke_) z << SEG_BODY_DB_GROW_BLEND_APPEND_TAIL;
				}
				else if (paint_mode_==3) {
					z << SEG_BODY_DB_GROW_BLEND_ERASE;
					if (use_stroke_) z << SEG_BODY_DB_GROW_BLEND_ERASE_STROKE_TAIL;
				}
				break;
			case LBL_SHDR_INITIALIZE:
				z << SEG_HEAD_LIT;
				z << VOL_DATA_VOLUME_LOOKUP_130;
				z << VOL_COMPUTED_GM_INVALIDATE;
				z << VOL_TRANSFER_FUNCTION_SIN_COLOR_L;
				if (paint_mode_==0)
				{
					if (use_2d_)
						z << SEG_BODY_LABEL_INITIALIZE;
					else
						z << SEG_BODY_LABEL_INITIALIZE_NOMASK;
					z << SEG_BODY_LABEL_INIT_ORDER;
				}
				else if (paint_mode_==1)
				{
					z << SEG_BODY_LABEL_INIT_POSTER;
					z << SEG_BODY_LABEL_INIT_ORDER;
				}
				else if (paint_mode_==2)
				{
					if (use_2d_)
						z << SEG_BODY_LABEL_INITIALIZE;
					else
						z << SEG_BODY_LABEL_INITIALIZE_NOMASK;
					z << SEG_BODY_LABEL_INIT_COPY;
				}
				else if (paint_mode_==3)
				{
					z << SEG_BODY_LABEL_INIT_POSTER;
					z << SEG_BODY_LABEL_INIT_COPY;
				}
				break;
			case LBL_SHDR_MIF:
				//if (paint_mode_==0 || paint_mode_==2)
				//	z << SEG_BODY_LABEL_MAX_FILTER;
				//else if (paint_mode_==1 || paint_mode_==3)
				//	z << SEG_BODY_LABEL_MIF_POSTER;
				z << SEG_HEAD_LIT;
				z << VOL_DATA_VOLUME_LOOKUP_130;
				z << VOL_MEASURE_GM_LOOKUP;
				z << VOL_TRANSFER_FUNCTION_SIN_COLOR_L;
				//z << SEG_BODY_DB_GROW_MEASURE;
				//z << SEG_BODY_DB_GROW_STOP_FUNC_MEASURE;
					z << SEG_BODY_LABEL_MAX_FILTER;
				break;
			case FLT_SHDR_NR:
				z << FLT_BODY_NR;
				break;
			}
		}

		//tail
		z << SEG_TAIL;

		s = z.str();
        
        std::cout << s << std::endl;
        
		return false;
	}

	SegShaderFactory::SegShaderFactory()
		: prev_shader_(-1)
	{}

	SegShaderFactory::SegShaderFactory(std::vector<vks::VulkanDevice*> &devices)
		: prev_shader_(-1)
	{
		init(devices);
	}

	void SegShaderFactory::init(std::vector<vks::VulkanDevice*> &devices)
	{
		vdevices_ = devices;
		setupDescriptorSetLayout();
	}

	SegShaderFactory::~SegShaderFactory()
	{
		for(unsigned int i=0; i<shader_.size(); i++)
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

	ShaderProgram* SegShaderFactory::shader(VkDevice device, int type, int paint_mode, int hr_mode,
		bool use_2d, bool shading, int peel, bool clip, bool hiqual, bool use_stroke, bool stroke_clear, int out_bytes, bool use_abs)
	{
		if(prev_shader_ >= 0)
		{
			if(shader_[prev_shader_]->match(device, type, paint_mode, hr_mode, use_2d, shading, peel, clip, hiqual, use_stroke, stroke_clear, out_bytes, use_abs))
			{
				return shader_[prev_shader_]->program();
			}
		}
		for(unsigned int i=0; i<shader_.size(); i++)
		{
			if(shader_[i]->match(device, type, paint_mode, hr_mode, use_2d, shading, peel, clip, hiqual, use_stroke, stroke_clear, out_bytes, use_abs))
			{
				prev_shader_ = i;
				return shader_[i]->program();
			}
		}

		SegShader* s = new SegShader(device, type, paint_mode, hr_mode, use_2d, shading, peel, clip, hiqual, use_stroke, stroke_clear, out_bytes, use_abs);
		if(s->create())
		{
			delete s;
			return 0;
		}
		shader_.push_back(s);
		prev_shader_ = (int)shader_.size()-1;
		return s->program();
	}
	
	void SegShaderFactory::setupDescriptorSetLayout()
	{
		for (auto vdev : vdevices_)
		{
			VkDevice device = vdev->logicalDevice;


			std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = 
			{
				//mask
				vks::initializers::descriptorSetLayoutBinding(
				VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				VK_SHADER_STAGE_COMPUTE_BIT,
				0),

				//uniform buffer
				vks::initializers::descriptorSetLayoutBinding(
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 
				VK_SHADER_STAGE_COMPUTE_BIT,
				1)
			};

			int offset = 2;
			for (int i = 0; i < SEG_SAMPLER_NUM; i++)
			{
				setLayoutBindings.push_back(
					vks::initializers::descriptorSetLayoutBinding(
					VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 
					VK_SHADER_STAGE_COMPUTE_BIT,
					offset+i)
					);
			}

			offset += SEG_SAMPLER_NUM;
			//label
			setLayoutBindings.push_back(
				vks::initializers::descriptorSetLayoutBinding(
					VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					VK_SHADER_STAGE_COMPUTE_BIT,
					offset)
			);
			//stroke
			setLayoutBindings.push_back(
				vks::initializers::descriptorSetLayoutBinding(
					VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					VK_SHADER_STAGE_COMPUTE_BIT,
					offset + 1)
			);
			
			VkDescriptorSetLayoutCreateInfo descriptorLayout = 
				vks::initializers::descriptorSetLayoutCreateInfo(
				setLayoutBindings.data(),
				static_cast<uint32_t>(setLayoutBindings.size()));

			descriptorLayout.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
			descriptorLayout.pNext = nullptr;

			SegPipeline pipe;
			VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &pipe.descriptorSetLayout));

			VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
				vks::initializers::pipelineLayoutCreateInfo(
				&pipe.descriptorSetLayout,
				1);

			VkPushConstantRange pushConstantRange = {};
			pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
			pushConstantRange.size = sizeof(SegShaderFactory::SegCompShaderBrickConst);
			pushConstantRange.offset = 0;

			// Push constant ranges are part of the pipeline layout
			pPipelineLayoutCreateInfo.pushConstantRangeCount = 1;
			pPipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;

			VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &pipe.pipelineLayout));

			pipeline_[vdev] = pipe;
		}
	}

	void SegShaderFactory::getDescriptorSetWriteUniforms(vks::VulkanDevice* vdev, SegUniformBufs& uniformBuffers, std::vector<VkWriteDescriptorSet>& writeDescriptorSets)
	{
		VkDevice device = vdev->logicalDevice;

		writeDescriptorSets.push_back(
			vks::initializers::writeDescriptorSet(
				VK_NULL_HANDLE,
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				1,
				&uniformBuffers.frag_base.descriptor)
		);
	}
	
	// Prepare and initialize uniform buffer containing shader uniforms
	void SegShaderFactory::prepareUniformBuffers(std::map<vks::VulkanDevice*, SegUniformBufs>& uniformBuffers)
	{
		for (auto vulkanDev : vdevices_)
		{
			SegUniformBufs uniformbufs;

			VK_CHECK_RESULT(vulkanDev->createBuffer(
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
				&uniformbufs.frag_base,
				sizeof(SegCompShaderBaseUBO)));

			VK_CHECK_RESULT(uniformbufs.frag_base.map());

			uniformBuffers[vulkanDev] = uniformbufs;
		}
	}

	void SegShaderFactory::updateUniformBuffers(SegUniformBufs& uniformBuffers, SegCompShaderBaseUBO fubo)
	{
		memcpy(uniformBuffers.frag_base.mapped, &fubo, sizeof(fubo));
	}


} // end namespace FLIVR

