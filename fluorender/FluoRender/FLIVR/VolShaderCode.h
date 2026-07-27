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

namespace FLIVR
{
#define DEFAULT_FRAGMENT_CODE \
	"void main() {\n" \
	"    FragColor = vec4(1,1,1,1);\n" \
	"}\n" 

#define VOL_INPUTS \
	"#pragma optionNV(inline all)\n" \
	"#pragma optionNV(fastmath on)\n" \
	"#pragma optionNV(ifcvt none)\n" \
	"#pragma optionNV(strict on)\n" \
	"#pragma optionNV(unroll all)\n" \
	"layout(location = 0) in vec3 OutVertex;\n" \
	"layout(location = 1) in vec3 OutTexture;\n"

#define VOL_INPUTS_FOG \
	"layout(location = 2) in vec4 OutFogCoord;\n"

#define VOL_OUTPUTS \
	"layout(location = 0) out vec4 FragColor;\n"

#define VOL_ID_COLOR_OUTPUTS \
	"layout(location = 1) out vec4 IDColor;\n"

#define VOL_UNIFORMS_BASE \
	"// VOL_UNIFORMS_BASE\n" \
	"layout (binding = 1) uniform VolFragShaderBaseUBO {\n" \
	"	vec4 loc0;//(lx, ly, lz, alpha)\n" \
	"	vec4 loc1;//(ka, kd, ks, ns)\n" \
	"	vec4 loc2;//(scalar_scale, gm_scale, left_thresh, right_thresh)\n" \
	"	vec4 loc3;//(gamma, gm_thresh, offset, sw)\n" \
	"	vec4 loc5;//(spcx, spcy, spcz, max_id)\n" \
	"	vec4 loc6;//(r, g, b, 0.0) or (1/vx, 1/vy, luminance, depth_mode)\n" \
	"	vec4 loc7;//(1/vx, 1/vy, 0, 0)\n" \
	"	vec4 loc8;//(int, start, end, 0.0)\n" \
	"	vec4 loc10; //plane0\n" \
	"	vec4 loc11; //plane1\n" \
	"	vec4 loc12; //plane2\n" \
	"	vec4 loc13; //plane3\n" \
	"	vec4 loc14; //plane4\n" \
	"	vec4 loc15; //plane5\n" \
	"} base;\n" \
	"\n" \
	"layout (binding = 2) uniform sampler3D tex0;//data volume\n" \
	"layout (binding = 3) uniform sampler3D tex1;//gm volume\n" \
	"\n" \

#define VOL_UNIFORMS_BRICK \
	"// VOL_UNIFORMS_BRICK\n" \
	"layout (push_constant) uniform VolFragShaderBrickConst {\n" \
	"	vec4 loc4;//(1/nx, 1/ny, 1/nz, 1/sample_rate)\n" \
	"	vec4 brkscale;//tex transform for bricking\n" \
	"	vec4 brktrans;//tex transform for bricking\n" \
	"	vec4 mskbrkscale;//tex transform for mask bricks\n" \
	"	vec4 mskbrktrans;//tex transform for mask bricks\n" \
	"} brk;" \
	"\n"

#define VOL_UNIFORMS_INDEX_COLOR \
	"//VOL_UNIFORMS_INDEX_COLOR\n" \
	"layout (binding = 7) uniform sampler2D tex5;\n" \
	"layout (binding = 8) uniform sampler2D tex7;\n" \
	"\n"

#define VOL_UNIFORMS_INDEX_COLOR_D \
    "//VOL_UNIFORMS_INDEX_COLOR_D\n" \
    "layout (binding = 7) uniform sampler2D tex5;\n" \
	"layout (binding = 8) uniform sampler2D tex7;\n" \
    "\n"

#define VOL_UNIFORMS_DP \
	"//VOL_UNIFORMS_DP\n" \
	"layout (binding = 16) uniform sampler2D tex14;//depth texture 1\n" \
	"layout (binding = 17) uniform sampler2D tex15;//depth texture 2\n" \
	"\n"

#define VOL_UNIFORMS_MASK \
	"//VOL_UNIFORMS_MASK\n" \
	"layout (binding = 4) uniform sampler3D tex2;//3d mask volume\n" \
	"\n"

#define VOL_UNIFORMS_LABEL \
	"//VOL_UNIFORMS_LABEL\n" \
	"layout (binding = 5) uniform usampler3D tex3;//3d label volume\n" \
	"\n"

#define VOL_UNIFORMS_DEPTHMAP \
	"//VOL_UNIFORMS_DEPTHMAP\n" \
	"layout (binding = 6) uniform sampler2D tex4;//2d depth map\n" \
	"\n"

#define VOL_HEAD \
	"//VOL_HEAD\n" \
	"void main()\n" \
	"{\n" \
	"	vec4 TexCoord = vec4(OutTexture, 1.0);\n" \
	"	vec4 t = TexCoord;\n" \
	"\n"

#define VOL_HEAD_2DMAP_LOC \
	"	//VOL_HEAD_2DMAP_LOC\n" \
	"	vec2 fcf = vec2(gl_FragCoord.x*base.loc7.x, gl_FragCoord.y*base.loc7.y);\n" \
	"\n"

#define VOL_HEAD_DP_1 \
	"	//VOL_HEAD_DP_NEG\n" \
	"	if (texture(tex15, fcf).r < gl_FragCoord.z) discard;\n" \
	"\n"

#define VOL_HEAD_DP_2 \
	"	//VOL_HEAD_DP_POS\n" \
	"	if (texture(tex14, fcf).r > gl_FragCoord.z) discard;\n" \
	"\n"

#define VOL_HEAD_DP_3 \
	"	//VOL_HEAD_DP_BOTH\n" \
	"	if (texture(tex15, fcf).r < gl_FragCoord.z) discard;\n" \
	"	else if (texture(tex14, fcf).r > gl_FragCoord.z) discard;\n" \
	"\n"

#define VOL_HEAD_FOG \
	"	//VOL_HEAD_FOG\n" \
	"	vec4 fp;\n" \
	"	fp.x = base.loc8.x;\n" \
	"	fp.y = base.loc8.y;\n" \
	"	fp.z = base.loc8.z;\n" \
	"	fp.w = abs(OutFogCoord.z/OutFogCoord.w);\n" \
	"\n"

#define VOL_HEAD_CLIP \
	"	//VOL_HEAD_CLIP\n" \
	"	vec4 brickt = (t*brk.brkscale + brk.brktrans);\n" \
	"	if (dot(brickt.xyz, base.loc10.xyz)+base.loc10.w < 0.0 ||\n" \
	"		dot(brickt.xyz, base.loc11.xyz)+base.loc11.w < 0.0 ||\n" \
	"		dot(brickt.xyz, base.loc12.xyz)+base.loc12.w < 0.0 ||\n" \
	"		dot(brickt.xyz, base.loc13.xyz)+base.loc13.w < 0.0 ||\n" \
	"		dot(brickt.xyz, base.loc14.xyz)+base.loc14.w < 0.0 ||\n" \
	"		dot(brickt.xyz, base.loc15.xyz)+base.loc15.w < 0.0)\n" \
	"	{\n" \
	"		discard;//FragColor = vec4(0.0);\n" \
	"		return;\n" \
	"	}\n" \
	"\n"

#define VOL_HEAD_CLIP_FUNC \
	"	if (vol_clip_func(t))\n" \
	"	{\n" \
	"		discard;\n" \
	"		return;\n" \
	"	}\n" \
	"\n"

#define VOL_CLIP_FUNC \
	"//VOL_CLIP_FUNC\n" \
	"bool vol_clip_func(vec4 t)\n" \
	"{\n" \
	"	vec4 brickt = (t*brk.brkscale + brk.brktrans);\n" \
	"	if (dot(brickt.xyz, base.loc10.xyz)+base.loc10.w < 0.0 ||\n" \
	"		dot(brickt.xyz, base.loc11.xyz)+base.loc11.w < 0.0 ||\n" \
	"		dot(brickt.xyz, base.loc12.xyz)+base.loc12.w < 0.0 ||\n" \
	"		dot(brickt.xyz, base.loc13.xyz)+base.loc13.w < 0.0 ||\n" \
	"		dot(brickt.xyz, base.loc14.xyz)+base.loc14.w < 0.0 ||\n" \
	"		dot(brickt.xyz, base.loc15.xyz)+base.loc15.w < 0.0)\n" \
	"		return true;\n" \
	"	else\n" \
	"		return false;\n" \
	"}\n" \
	"\n"

#define VOL_HEAD_HIDE_OUTSIDE_MASK \
	"	//VOL_HEAD_HIDE_OUTSIDE_MASK\n" \
	"	vec4 maskt = t*brk.mskbrkscale + brk.mskbrktrans;\n" \
	"	vec4 maskcheck = texture(tex2, maskt.stp); //get mask value\n" \
	"	if (maskcheck.x <= 0.5)\n" \
	"	{\n" \
	"		discard;\n" \
	"		return;\n" \
	"	}\n" \
	"\n"

#define VOL_HEAD_HIDE_INSIDE_MASK \
	"	//VOL_HEAD_HIDE_INSIDE_MASK\n" \
	"	vec4 maskt = t*brk.mskbrkscale + brk.mskbrktrans;\n" \
	"	vec4 maskcheck = texture(tex2, maskt.stp); //get mask value\n" \
	"	if (maskcheck.x > 0.5)\n" \
	"	{\n" \
	"		discard;\n" \
	"		return;\n" \
	"	}\n" \
	"\n"

#define VOL_HEAD_LIT \
	"	//VOL_HEAD_LIT\n" \
	"	vec4 l = base.loc0; // {lx, ly, lz, alpha}\n" \
	"	vec4 k = base.loc1; // {ka, kd, ks, ns}\n" \
	"	k.x = k.x>1.0?log2(3.0-k.x):k.x;\n" \
	"	vec4 n, w;\n" \
	"	if (l.w == 0.0) { discard; return; }\n" \
	"\n"

#define VOL_TAIL \
	"//VOL_TAIL\n" \
	"}\n" \
	"\n"

#define VOL_DATA_VOLUME_LOOKUP \
	"	//VOL_DATA_VOLUME_LOOKUP\n" \
	"	vec4 v = texture(tex0, t.stp);\n" \
	"\n"

#define VOL_DATA_VOLUME_LOOKUP_130 \
	"	//VOL_DATA_VOLUME_LOOKUP_130\n" \
	"	vec4 v = texture(tex0, t.stp);\n" \
	"\n"

#define VOL_GRAD_COMPUTE_LO \
	"	//VOL_GRAD_COMPUTE_LO\n" \
	"	vec4 dir = brk.loc4; // \n" \
	"	vec4 r, p; \n" \
	"	v = vec4(v.x); \n" \
	"	n = vec4(0.0); \n" \
	"	w = vec4(0.0);\n" \
	"	w.x = dir.x; \n" \
	"	p = clamp(TexCoord + w, 0.0, 1.0); \n" \
	"	r = texture(tex0, p.stp); \n" \
	"	n.x = v.x - r.x; \n" \
	"	w = vec4(0.0); \n" \
	"	w.y = dir.y; \n" \
	"	p = clamp(TexCoord + w, 0.0, 1.0); \n" \
	"	r = texture(tex0, p.stp); \n" \
	"	n.y = v.y - r.x; \n" \
	"	w = vec4(0.0); \n" \
	"	w.z = dir.z; \n" \
	"	p = clamp(TexCoord + w, 0.0, 1.0); \n" \
	"	r = texture(tex0, p.stp); \n" \
	"	n.z = v.z - r.x; \n" \
	"	p.y = length(n.xyz); \n" \
	"	p.y = 0.5 * (base.loc2.x<0.0?(1.0+p.y*base.loc2.x):p.y*base.loc2.x); \n" \
	"\n"

#define VOL_GRAD_COMPUTE_HI \
	"	// VOL_GRAD_COMPUTE_HI\n" \
	"	vec4 dir = brk.loc4;//(1/nx, 1/ny, 1/nz, 1/sample_rate)\n" \
	"	vec4 r, p; \n" \
	"	v = vec4(v.x); \n" \
	"	n = vec4(0.0); \n" \
	"	w = vec4(0.0);\n" \
	"	w.x = dir.x; \n" \
	"	p = clamp(TexCoord + w, 0.0, 1.0); \n" \
	"	r = texture(tex0, p.stp); \n" \
	"	n.x = r.x + n.x; \n" \
	"	p = clamp(TexCoord - w, 0.0, 1.0); \n" \
	"	r = texture(tex0, p.stp); \n" \
	"	n.x = r.x - n.x; \n" \
	"	w = vec4(0.0); \n" \
	"	w.y = dir.y; \n" \
	"	p = clamp(TexCoord + w, 0.0, 1.0); \n" \
	"	r = texture(tex0, p.stp); \n" \
	"	n.y = r.x + n.y; \n" \
	"	p = clamp(TexCoord - w, 0.0, 1.0); \n" \
	"	r = texture(tex0, p.stp); \n" \
	"	n.y = r.x - n.y; \n" \
	"	w = vec4(0.0); \n" \
	"	w.z = dir.z; \n" \
	"	p = clamp(TexCoord + w, 0.0, 1.0); \n" \
	"	r = texture(tex0, p.stp); \n" \
	"	n.z = r.x + n.z; \n" \
	"	p = clamp(TexCoord - w, 0.0, 1.0); \n" \
	"	r = texture(tex0, p.stp); \n" \
	"	n.z = r.x - n.z; \n" \
	"	p.y = length(n.xyz); \n" \
	"	p.y = 0.5 * (base.loc2.x<0.0?(1.0+p.y*base.loc2.x):p.y*base.loc2.x); \n" \
	"\n"

#define VOL_ID_GRAD_COMPUTE_HI \
	"	// VOL_GRAD_COMPUTE_HI\n" \
	"	vec4 dir = brk.loc4;//(1/nx, 1/ny, 1/nz, 1/sample_rate)\n" \
	"	vec4 r, p; \n" \
	"	v = vec4(v.x); \n" \
	"	n = vec4(0.0); \n" \
	"	w = vec4(0.0);\n" \
	"	w.x = dir.x; \n" \
	"	p = clamp(TexCoord + w, 0.0, 1.0); \n" \
	"	r = texture(tex0, p.stp); \n" \
	"	r.x = (v.x==r.x?1.0:0.0) ; \n" \
	"	n.x = r.x + n.x; \n" \
	"	p = clamp(TexCoord - w, 0.0, 1.0); \n" \
	"	r = texture(tex0, p.stp); \n" \
	"	r.x = (v.x==r.x?1.0:0.0) ; \n" \
	"	n.x = r.x - n.x; \n" \
	"	w = vec4(0.0); \n" \
	"	w.y = dir.y; \n" \
	"	p = clamp(TexCoord + w, 0.0, 1.0); \n" \
	"	r = texture(tex0, p.stp); \n" \
	"	r.x = (v.x==r.x?1.0:0.0) ; \n" \
	"	n.y = r.x + n.y; \n" \
	"	p = clamp(TexCoord - w, 0.0, 1.0); \n" \
	"	r = texture(tex0, p.stp); \n" \
	"	r.x = (v.x==r.x?1.0:0.0) ; \n" \
	"	n.y = r.x - n.y; \n" \
	"	w = vec4(0.0); \n" \
	"	w.z = dir.z; \n" \
	"	p = clamp(TexCoord + w, 0.0, 1.0); \n" \
	"	r = texture(tex0, p.stp); \n" \
	"	r.x = (v.x==r.x?1.0:0.0) ; \n" \
	"	n.z = r.x + n.z; \n" \
	"	p = clamp(TexCoord - w, 0.0, 1.0); \n" \
	"	r = texture(tex0, p.stp); \n" \
	"	r.x = (v.x==r.x?1.0:0.0) ; \n" \
	"	n.z = r.x - n.z; \n" \
	"	p.y = length(n.xyz); \n" \
	"	p.y = 0.5 * (base.loc2.x<0.0?(1.0+p.y*base.loc2.x):p.y*base.loc2.x); \n" \
	"\n"

#define VOL_GRAD_COMPUTE_FUNC \
	"// VOL_GRAD_COMPUTE_FUNC\n" \
	"vec4 vol_grad_func(vec4 pos, vec4 dir)\n" \
	"{\n" \
	"	vec4 r, p;\n" \
	"	vec4 n = vec4(0.0);\n" \
	"	vec4 w = vec4(0.0);\n" \
	"	w.x = dir.x;\n" \
	"	p = clamp(pos+w, 0.0, 1.0);\n" \
	"	r = texture(tex0, p.stp);\n" \
	"	n.x = r.x + n.x;\n" \
	"	p = clamp(pos-w, 0.0, 1.0);\n" \
	"	r = texture(tex0, p.stp);\n" \
	"	n.x = r.x - n.x;\n" \
	"	w = vec4(0.0);\n" \
	"	w.y = dir.y;\n" \
	"	p = clamp(pos+w, 0.0, 1.0);\n" \
	"	r = texture(tex0, p.stp);\n" \
	"	n.y = r.x + n.y;\n" \
	"	p = clamp(pos-w, 0.0, 1.0);\n" \
	"	r = texture(tex0, p.stp);\n" \
	"	n.y = r.x - n.y;\n" \
	"	w = vec4(0.0);\n" \
	"	w.z = dir.z;\n" \
	"	p = clamp(pos+w, 0.0, 1.0);\n" \
	"	r = texture(tex0, p.stp);\n" \
	"	n.z = r.x + n.z;\n" \
	"	p = clamp(pos-w, 0.0, 1.0);\n" \
	"	r = texture(tex0, p.stp);\n" \
	"	n.z = 0.3 * (r.x - n.z);\n" \
	"	return n;\n" \
	"}\n"

#define VOL_BODY_SHADING \
	"	//VOL_BODY_SHADING\n" \
	"	n.xyz = normalize(n.xyz);\n" \
	"	n.w = dot(l.xyz, n.xyz); // calculate angle between light and normal. \n" \
	"	n.w = clamp(abs(n.w), 0.0, 1.0); // two-sided lighting, n.w = abs(cos(angle))  \n" \
	"	w = k; // w.x = weight*ka, w.y = weight*kd, w.z = weight*ks \n" \
	"	w.x = k.x - w.y; // w.x = ka - kd*weight \n" \
	"	w.x = w.x + k.y; // w.x = ka + kd - kd*weight \n" \
	"	n.z = pow(n.w, k.w); // n.z = abs(cos(angle))^ns \n" \
	"	n.w = (n.w * w.y) + w.x; // n.w = abs(cos(angle))*kd+ka\n" \
	"	n.z = w.z * n.z; // n.z = weight*ks*abs(cos(angle))^ns \n" \
	"\n"

#define VOL_COMPUTED_GM_LOOKUP \
	"	//VOL_COMPUTED_GM_LOOKUP\n" \
	"	v.y = p.y;\n" \
	"\n"

#define VOL_COMPUTED_GM_INVALIDATE \
	"	//VOL_COMPUTED_GM_INVALIDATE\n" \
	"	v.y = base.loc3.y;\n" \
	"\n"

#define VOL_TEXTURE_GM_LOOKUP \
	"	//VOL_TEXTURE_GM_LOOKUP\n" \
	"	v.y = texture(tex1, t.stp).x;\n" \
	"\n"

#define VOL_TRANSFER_FUNCTION_SIN_COLOR \
	"	//VOL_TRANSFER_FUNCTION_SIN_COLOR\n" \
	"	vec4 c;\n" \
	"	float tf_alp = 0.0;\n" \
	"	float alpha = 0.0;\n" \
	"	v.x = base.loc2.x<0.0?(1.0+v.x*base.loc2.x):v.x*base.loc2.x;\n" \
	"	if (v.x<base.loc2.z-base.loc3.w || v.x>base.loc2.w+base.loc3.w || v.y<base.loc3.y-base.loc3.w)\n" \
	"		c = vec4(0.0);\n" \
	"	else\n" \
	"	{\n" \
	"		v.x = (v.x<base.loc2.z?(base.loc3.w-base.loc2.z+v.x)/base.loc3.w:(v.x>base.loc2.w?(base.loc3.w-v.x+base.loc2.w)/base.loc3.w:1.0))*v.x;\n" \
	"		v.x = (v.y<base.loc3.y?(base.loc3.w-base.loc3.y+v.y)/base.loc3.w:1.0)*v.x;\n" \
	"		tf_alp = pow(clamp(v.x/base.loc3.z,\n" \
	"			base.loc3.x<1.0?-(base.loc3.x-1.0)*0.00001:0.0,\n" \
	"			base.loc3.x>1.0?0.9999:1.0), base.loc3.x);\n" \
	"		alpha = (1.0 - pow(clamp(1.0-tf_alp*l.w, 0.0, 1.0), brk.loc4.w)) / l.w;\n" \
	"		c = vec4(base.loc6.rgb*alpha*tf_alp, alpha);\n" \
	"	}\n" \
	"\n"

#define VOL_TRANSFER_FUNCTION_SIN_COLOR_SOLID \
	"	//VOL_TRANSFER_FUNCTION_SIN_COLOR_SOLID\n" \
	"	vec4 c;\n" \
	"	float tf_alp = 0.0;\n" \
	"	float alpha = 0.0;\n" \
	"	v.x = base.loc2.x<0.0?(1.0+v.x*base.loc2.x):v.x*base.loc2.x;\n" \
	"	if (v.x<base.loc2.z-base.loc3.w || v.x>base.loc2.w+base.loc3.w || v.y<base.loc3.y)\n" \
	"		c = vec4(0.0, 0.0, 0.0, 1.0);\n" \
	"	else\n" \
	"	{\n" \
	"		v.x = (v.x<base.loc2.z?(base.loc3.w-base.loc2.z+v.x)/base.loc3.w:(v.x>base.loc2.w?(base.loc3.w-v.x+base.loc2.w)/base.loc3.w:1.0))*v.x;\n" \
	"		tf_alp = pow(clamp(v.x/base.loc3.z,\n" \
	"			base.loc3.x<1.0?-(base.loc3.x-1.0)*0.00001:0.0,\n" \
	"			base.loc3.x>1.0?0.9999:1.0), base.loc3.x);\n" \
	"		c = vec4(base.loc6.rgb*tf_alp, 1.0);\n" \
	"	}\n" \
	"\n"
/*
#define VOL_TRANSFER_FUNCTION_SIN_COLOR_L \
	"	//VOL_TRANSFER_FUNCTION_SIN_COLOR_L\n" \
	"	vec4 c;\n" \
	"	float tf_alp = 0.0;\n" \
	"	float alpha = 0.0;\n" \
	"	v.x = base.loc2.x<0.0?(1.0+v.x*base.loc2.x):v.x*base.loc2.x;\n" \
	"	if (v.x<base.loc2.z-base.loc3.w || v.x>base.loc2.w+base.loc3.w || v.y<base.loc3.y)\n" \
	"		c = vec4(0.0);\n" \
	"	else\n" \
	"	{\n" \
	"		v.x = (v.x<base.loc2.z?(base.loc3.w-base.loc2.z+v.x)/base.loc3.w:(v.x>base.loc2.w?(base.loc3.w-v.x+base.loc2.w)/base.loc3.w:1.0))*v.x;\n" \
	"		tf_alp = pow(clamp(v.x/base.loc3.z,\n" \
	"			base.loc3.x<1.0?-(base.loc3.x-1.0)*0.00001:0.0,\n" \
	"			base.loc3.x>1.0?0.9999:1.0), base.loc3.x);\n" \
	"		c = vec4(tf_alp);\n" \
	"	}\n" \
	"\n"
*/
#define VOL_TRANSFER_FUNCTION_SIN_COLOR_L \
	"	//VOL_TRANSFER_FUNCTION_SIN_COLOR_L\n" \
	"	vec4 c;\n" \
	"	float tf_alp = 0.0;\n" \
	"	float alpha = 0.0;\n" \
	"	v.x = base.loc2.x<0.0?(1.0+v.x*base.loc2.x):v.x*base.loc2.x;\n" \
	"	c = vec4(v.x);\n" \
	"\n"

#define VOL_TRANSFER_FUNCTION_SIN_COLOR_ORG \
    "    //VOL_TRANSFER_FUNCTION_SIN_COLOR_ORG\n" \
    "    vec4 c;\n" \
    "    float tf_alp = 0.0;\n" \
    "    float alpha = 0.0;\n" \
    "    c = vec4(v.x);\n" \
    "\n"

#define VOL_TRANSFER_FUNCTION_SIN_COLOR_L_FUNC \
	"//VOL_TRANSFER_FUNCTION_SIN_COLOR_L_FUNC\n" \
	"vec4 vol_trans_sin_color_l(vec4 v)\n" \
	"{\n" \
	"	vec4 c;\n" \
	"	float tf_alp;\n" \
	"	v.x = base.loc2.x<0.0?(1.0+v.x*base.loc2.x):v.x*base.loc2.x;\n" \
	"	if (v.x<base.loc2.z-base.loc3.w || v.x>base.loc2.w+base.loc3.w || v.y<base.loc3.y)\n" \
	"		c = vec4(0.0);\n" \
	"	else\n" \
	"	{\n" \
	"		v.x = (v.x<base.loc2.z?(base.loc3.w-base.loc2.z+v.x)/base.loc3.w:(v.x>base.loc2.w?(base.loc3.w-v.x+base.loc2.w)/base.loc3.w:1.0))*v.x;\n" \
	"		tf_alp = pow(clamp(v.x/base.loc3.z,\n" \
	"			base.loc3.x<1.0?-(base.loc3.x-1.0)*0.00001:0.0,\n" \
	"			base.loc3.x>1.0?0.9999:1.0), base.loc3.x);\n" \
	"		c = vec4(tf_alp);\n" \
	"	}\n" \
	"	return c;\n" \
	"}\n" \
	"\n"

#define VOL_COMMON_TRANSFER_FUNCTION_CALC \
	"		//VOL_COMMON_TRANSFER_FUNCTION_CALC\n" \
	"		tf_alp = pow(clamp(v.x/base.loc3.z,\n" \
	"			base.loc3.x<1.0?-(base.loc3.x-1.0)*0.00001:0.0,\n" \
	"			base.loc3.x>1.0?0.9999:1.0), base.loc3.x);\n"

#define VOL_COLORMAP_CALC0 \
	"		//VOL_COLORMAP_CALC0\n" \
	"		rb.r = clamp(4.0*valu - 2.0, 0.0, 1.0);\n" \
	"		rb.g = clamp(valu<0.5 ? 4.0*valu : -4.0*valu+4.0, 0.0, 1.0);\n" \
	"		rb.b = clamp(-4.0*valu+2.0, 0.0, 1.0);\n"

#define VOL_COLORMAP_CALC1 \
	"		//VOL_COLORMAP_CALC1\n" \
	"		rb.r = clamp(-4.0*valu+2.0, 0.0, 1.0);\n" \
	"		rb.g = clamp(valu<0.5 ? 4.0*valu : -4.0*valu+4.0, 0.0, 1.0);\n" \
	"		rb.b = clamp(4.0*valu - 2.0, 0.0, 1.0);\n"

#define VOL_COLORMAP_CALC2 \
	"		//VOL_COLORMAP_CALC2\n" \
	"		rb.r = clamp(2.0*valu, 0.0, 1.0);\n" \
	"		rb.g = clamp(4.0*valu - 2.0, 0.0, 1.0);\n" \
	"		rb.b = clamp(4.0*valu - 3.0, 0.0, 1.0);\n"

#define VOL_COLORMAP_CALC3 \
	"		//VOL_COLORMAP_CALC3\n" \
	"		rb.r = clamp(valu, 0.0, 1.0);\n" \
	"		rb.g = clamp(1.0-valu, 0.0, 1.0);\n" \
	"		rb.b = 1.0;\n"

#define VOL_COLORMAP_CALC4 \
	"		//VOL_COLORMAP_CALC4\n" \
	"		rb.r = clamp(valu<0.5?valu*0.9+0.25:0.7, 0.0, 1.0);\n" \
	"		rb.g = clamp(valu<0.5?valu*0.8+0.3:(1.0-valu)*1.4, 0.0, 1.0);\n" \
	"		rb.b = clamp(valu<0.5?valu*(-0.1)+0.75:(1.0-valu)*1.1+0.15, 0.0, 1.0);\n"

#define VOL_TRANSFER_FUNCTION_COLORMAP \
	"	//VOL_TRANSFER_FUNCTION_COLORMAP\n" \
	"	vec4 c;\n" \
	"	float tf_alp = 0.0;\n" \
	"	float alpha = 0.0;\n" \
	"	v.x = base.loc2.x<0.0?(1.0+v.x*base.loc2.x):v.x*base.loc2.x;\n" \
	"	if (v.x<base.loc2.z-base.loc3.w || v.x>base.loc2.w+base.loc3.w || v.y<base.loc3.y)\n" \
	"		c = vec4(0.0);\n" \
	"	else\n" \
	"	{\n" \
	"		v.x = (v.x<base.loc2.z?(base.loc3.w-base.loc2.z+v.x)/base.loc3.w:(v.x>base.loc2.w?(base.loc3.w-v.x+base.loc2.w)/base.loc3.w:1.0))*v.x;\n" \
	"		vec4 rb = vec4(0.0);\n"

#define VOL_TRANSFER_FUNCTION_COLORMAP_VALU0 \
	"		//VOL_TRANSFER_FUNCTION_COLORMAP_VALU\n" \
	"		float valu = (v.x-base.loc6.x)/base.loc6.z;\n"

#define VOL_TRANSFER_FUNCTION_COLORMAP_VALU1 \
	"		//VOL_TRANSFER_FUNCTION_COLORMAP_VALU_Z\n" \
	"		vec4 tt = t*brk.brkscale + brk.brktrans;\n" \
	"		float valu = (1.0-tt.z-base.loc6.x)/base.loc6.z;\n"

#define VOL_TRANSFER_FUNCTION_COLORMAP_VALU2 \
	"		//VOL_TRANSFER_FUNCTION_COLORMAP_VALU_Z\n" \
	"		vec4 tt = t*brk.brkscale + brk.brktrans;\n" \
	"		float valu = (1.0-tt.y-base.loc6.x)/base.loc6.z;\n"

#define VOL_TRANSFER_FUNCTION_COLORMAP_VALU3 \
	"		//VOL_TRANSFER_FUNCTION_COLORMAP_VALU_Z\n" \
	"		vec4 tt = t*brk.brkscale + brk.brktrans;\n" \
	"		float valu = (1.0-tt.x-base.loc6.x)/base.loc6.z;\n"

#define VOL_TRANSFER_FUNCTION_COLORMAP_RESULT \
	"		//VOL_TRANSFER_FUNCTION_COLORMAP_RESULT\n" \
	"		float alpha = (1.0 - pow(clamp(1.0-tf_alp*l.w, 0.0, 1.0), brk.loc4.w)) / l.w;\n" \
	"		c = vec4(rb.rgb*alpha*tf_alp, alpha);\n" \
	"	}\n" \
	"\n"

#define VOL_TRANSFER_FUNCTION_COLORMAP_SOLID \
	"	//VOL_TRANSFER_FUNCTION_COLORMAP_SOLID\n" \
	"	vec4 c;\n" \
	"	float tf_alp = 0.0;\n" \
	"	float alpha = 0.0;\n" \
	"	v.x = base.loc2.x<0.0?(1.0+v.x*base.loc2.x):v.x*base.loc2.x;\n" \
	"	if (v.x<base.loc2.z-base.loc3.w || v.x>base.loc2.w+base.loc3.w || v.y<base.loc3.y)\n" \
	"		c = vec4(0.0, 0.0, 0.0, 1.0);\n" \
	"	else\n" \
	"	{\n" \
	"		v.x = (v.x<base.loc2.z?(base.loc3.w-base.loc2.z+v.x)/base.loc3.w:(v.x>base.loc2.w?(base.loc3.w-v.x+base.loc2.w)/base.loc3.w:1.0))*v.x;\n" \
	"		vec4 rb = vec4(0.0);\n"

#define VOL_TRANSFER_FUNCTION_COLORMAP_SOLID_RESULT \
	"		//VOL_TRANSFER_FUNCTION_COLORMAP_SOLID_RESULT\n" \
	"		c = vec4(rb.rgb, 1.0);\n" \
	"	}\n" \
	"\n"

#define VOL_TRANSFER_FUNCTION_DEPTHMAP \
	"	//VOL_TRANSFER_FUNCTION_DEPTHMAP\n" \
	"	vec4 c;\n" \
	"	float tf_alp = 0.0;\n" \
	"	float alpha = 0.0;\n" \
	"	v.x = base.loc2.x<0.0?(1.0+v.x*base.loc2.x):v.x*base.loc2.x;\n" \
	"	if (v.x<base.loc2.z-base.loc3.w || v.x>base.loc2.w+base.loc3.w || v.y<base.loc3.y)\n" \
	"		c = vec4(0.0);\n" \
	"	else\n" \
	"	{\n" \
	"		v.x = (v.x<base.loc2.z?(base.loc3.w-base.loc2.z+v.x)/base.loc3.w:(v.x>base.loc2.w?(base.loc3.w-v.x+base.loc2.w)/base.loc3.w:1.0))*v.x;\n" \
	"		tf_alp = pow(clamp(v.x/base.loc3.z,\n" \
	"			base.loc3.x<1.0?-(base.loc3.x-1.0)*0.00001:0.0,\n" \
	"			base.loc3.x>1.0?0.9999:1.0), base.loc3.x);\n" \
	"		float alpha = tf_alp;\n" \
	"		c = vec4(vec3(alpha*tf_alp), alpha);\n" \
	"	}\n" \
	"\n"

#define VOL_INDEX_COLOR_BODY \
	"	//VOL_INDEX_COLOR_BODY\n" \
	"	vec4 v;\n" \
	"	uint label = uint(texture(tex0, t.stp).x*base.loc5.w+0.5); //get mask value\n" \
	"	vec4 c = texture(tex7, vec2((float(label%uint(256))+0.5)/256.0, (float(label/256)+0.5)/256.0));\n" \
	"	vec4 col = texture(tex5, gl_FragCoord.xy*base.loc6.xy);\n" \
	"	if (col.rgb == c.rgb)\n" \
	"	{\n" \
	"		discard;\n" \
	"		return;\n" \
	"	}\n" \
	"	IDColor = c;\n" \
	"	c.rgb = (c.rgb+col.rgb)*base.loc6.z;\n" \
	"\n"

#define VOL_INDEX_COLOR_BODY_SHADE \
	"	//VOL_INDEX_COLOR_BODY\n" \
	"	vec4 v;\n" \
	"	uint id = uint(texture(tex0, t.stp).x*base.loc5.w+0.5); //get mask value\n" \
	"	vec4 c = texture(tex7, vec2((float(id%uint(256))+0.5)/256.0, (float(id/256)+0.5)/256.0));\n" \
	"	vec4 col = texture(tex5, gl_FragCoord.xy*base.loc6.xy);\n" \
	"	if (col.rgb == c.rgb)\n" \
	"	{\n" \
	"		discard;\n" \
	"		return;\n" \
	"	}\n" \
	"	IDColor = c;\n" \
	"	// VOL_GRAD_COMPUTE_HI\n" \
	"	vec4 dir = brk.loc4*2.0;//(1/nx, 1/ny, 1/nz, 1/sample_rate)\n" \
	"	vec4 p; \n" \
	"	uint r; \n" \
	"	v = vec4(0.0); \n" \
	"	n = vec4(0.0); \n" \
	"	w = vec4(0.0);\n" \
	"	w.x = dir.x; \n" \
	"	p = clamp(TexCoord + w, 0.0, 1.0); \n" \
	"	r = uint(texture(tex0, p.stp).x*base.loc5.w+0.5); \n" \
	"	v.x = (id==r?0.5:0.0) ; \n" \
	"	n.x = v.x + n.x; \n" \
	"	p = clamp(TexCoord - w, 0.0, 1.0); \n" \
	"	r = uint(texture(tex0, p.stp).x*base.loc5.w+0.5); \n" \
	"	v.x = (id==r?0.5:0.0) ; \n" \
	"	n.x = v.x - n.x; \n" \
	"	w = vec4(0.0); \n" \
	"	w.y = dir.y; \n" \
	"	p = clamp(TexCoord + w, 0.0, 1.0); \n" \
	"	r = uint(texture(tex0, p.stp).x*base.loc5.w+0.5); \n" \
	"	v.x = (id==r?0.5:0.0) ; \n" \
	"	n.y = v.x + n.y; \n" \
	"	p = clamp(TexCoord - w, 0.0, 1.0); \n" \
	"	r = uint(texture(tex0, p.stp).x*base.loc5.w+0.5); \n" \
	"	v.x = (id==r?0.5:0.0) ; \n" \
	"	n.y = v.x - n.y; \n" \
	"	w = vec4(0.0); \n" \
	"	w.z = dir.z; \n" \
	"	p = clamp(TexCoord + w, 0.0, 1.0); \n" \
	"	r = uint(texture(tex0, p.stp).x*base.loc5.w+0.5); \n" \
	"	v.x = (id==r?0.5:0.0) ; \n" \
	"	n.z = v.x + n.z; \n" \
	"	p = clamp(TexCoord - w, 0.0, 1.0); \n" \
	"	r = uint(texture(tex0, p.stp).x*base.loc5.w+0.5); \n" \
	"	v.x = (id==r?0.5:0.0) ; \n" \
	"	n.z = v.x - n.z; \n" \
	"	p.y = length(n.xyz); \n" \
	"	p.y = 0.5 * (base.loc2.x<0.0?(1.0+p.y*base.loc2.x):p.y*base.loc2.x); \n" \
	"\n" \
	"	//VOL_BODY_SHADING\n" \
	"	n.xyz = normalize(n.xyz);\n" \
	"	n.w = dot(l.xyz, n.xyz); // calculate angle between light and normal. \n" \
	"	n.w = clamp(abs(n.w), 0.0, 1.0); // two-sided lighting, n.w = abs(cos(angle))  \n" \
	"	w = k; // w.x = weight*ka, w.y = weight*kd, w.z = weight*ks \n" \
	"	w.x = k.x - w.y; // w.x = ka - kd*weight \n" \
	"	w.x = w.x + k.y; // w.x = ka + kd - kd*weight \n" \
	"	n.z = pow(n.w, k.w); // n.z = abs(cos(angle))^ns \n" \
	"	n.w = (n.w * w.y) + w.x; // n.w = abs(cos(angle))*kd+ka\n" \
	"	n.z = w.z * n.z; // n.z = weight*ks*abs(cos(angle))^ns \n" \
	"\n" \
	"	//VOL_COMPUTED_GM_LOOKUP\n" \
	"	v.y = p.y;\n" \
	"\n" \
	"	//VOL_COLOR_OUTPUT\n" \
	"	c.xyz = (c.xyz+col.xyz)*clamp(1.0-base.loc1.x, 0.0, 1.0) + base.loc1.x*(c.xyz+col.xyz)*(base.loc1.y > 0.0?(n.w + n.z):1.0);\n" \
	"	c.xyz *= pow(1.0 - base.loc1.x/2.0, 2.0) + 1.0;\n" \
	"	c.rgb = c.rgb*base.loc6.z;\n" \
	"\n"
/*
 #define VOL_INDEX_COLOR_BODY_SHADE \
	"	//VOL_INDEX_COLOR_BODY\n" \
	"	vec4 v;\n" \
	"	uint id = uint(texture(tex0, t.stp).x*base.loc5.w+0.5); //get mask value\n" \
	"	vec4 c = texture(tex7, vec2((float(id%uint(256))+0.5)/256.0, (float(id/256)+0.5)/256.0));\n" \
	"	IDColor = c;\n" \
	"	if (id == 0 || c.rgb == vec3(0.0))\n" \
	"	{\n" \
	"		discard;\n" \
	"		return;\n" \
	"	}\n" \
	"	// VOL_GRAD_COMPUTE_HI\n" \
	"	vec4 dir = brk.loc4*1.5;//(1/nx, 1/ny, 1/nz, 1/sample_rate)\n" \
	"	vec4 p; \n" \
	"	uint r; \n" \
	"	uint df = 0; \n" \
	"	v = vec4(0.0); \n" \
	"	n = vec4(0.0); \n" \
	"	w = vec4(0.0);\n" \
	"	w.x = dir.x; \n" \
	"	p = clamp(TexCoord + w, 0.0, 1.0); \n" \
	"	r = uint(texture(tex0, p.stp).x*base.loc5.w+0.5); \n" \
	"	v.x = (id==r?0.5:0.0) ; \n" \
	"	df = df + (id!=r?1:0); \n" \
	"	n.x = v.x + n.x; \n" \
	"	p = clamp(TexCoord - w, 0.0, 1.0); \n" \
	"	r = uint(texture(tex0, p.stp).x*base.loc5.w+0.5); \n" \
	"	v.x = (id==r?0.5:0.0) ; \n" \
	"	df = df + (id!=r?1:0); \n" \
	"	n.x = v.x - n.x; \n" \
	"	w = vec4(0.0); \n" \
	"	w.y = dir.y; \n" \
	"	p = clamp(TexCoord + w, 0.0, 1.0); \n" \
	"	r = uint(texture(tex0, p.stp).x*base.loc5.w+0.5); \n" \
	"	v.x = (id==r?0.5:0.0) ; \n" \
	"	df = df + (id!=r?1:0); \n" \
	"	n.y = v.x + n.y; \n" \
	"	p = clamp(TexCoord - w, 0.0, 1.0); \n" \
	"	r = uint(texture(tex0, p.stp).x*base.loc5.w+0.5); \n" \
	"	v.x = (id==r?0.5:0.0) ; \n" \
	"	df = df + (id!=r?1:0); \n" \
	"	n.y = v.x - n.y; \n" \
	"	w = vec4(0.0); \n" \
	"	w.z = dir.z; \n" \
	"	p = clamp(TexCoord + w, 0.0, 1.0); \n" \
	"	r = uint(texture(tex0, p.stp).x*base.loc5.w+0.5); \n" \
	"	v.x = (id==r?0.5:0.0) ; \n" \
	"	df = df + (id!=r?1:0); \n" \
	"	n.z = v.x + n.z; \n" \
	"	p = clamp(TexCoord - w, 0.0, 1.0); \n" \
	"	r = uint(texture(tex0, p.stp).x*base.loc5.w+0.5); \n" \
	"	v.x = (id==r?0.5:0.0) ; \n" \
	"	df = df + (id!=r?1:0); \n" \
	"	n.z = v.x - n.z; \n" \
	"	p.y = length(n.xyz); \n" \
	"	p.y = 0.5 * (base.loc2.x<0.0?(1.0+p.y*base.loc2.x):p.y*base.loc2.x); \n" \
	"	if (df == 0)\n" \
	"	{\n" \
	"		discard;\n" \
	"		return;\n" \
	"	}\n" \
	"\n" \
	"	//VOL_BODY_SHADING\n" \
	"	n.xyz = normalize(n.xyz);\n" \
	"	n.w = dot(l.xyz, n.xyz); // calculate angle between light and normal. \n" \
	"	n.w = clamp(abs(n.w), 0.0, 1.0); // two-sided lighting, n.w = abs(cos(angle))  \n" \
	"	w = k; // w.x = weight*ka, w.y = weight*kd, w.z = weight*ks \n" \
	"	w.x = k.x - w.y; // w.x = ka - kd*weight \n" \
	"	w.x = w.x + k.y; // w.x = ka + kd - kd*weight \n" \
	"	n.z = pow(n.w, k.w); // n.z = abs(cos(angle))^ns \n" \
	"	n.w = (n.w * w.y) + w.x; // n.w = abs(cos(angle))*kd+ka\n" \
	"	n.z = w.z * n.z; // n.z = weight*ks*abs(cos(angle))^ns \n" \
	"\n" \
	"	//VOL_COMPUTED_GM_LOOKUP\n" \
	"	v.y = p.y;\n" \
	"\n" \
	"	//VOL_ALPHA\n" \
	"	float alpha = 0.0;\n" \
	"	alpha = (1.0 - pow(clamp(1.0-l.w, 0.0, 1.0), brk.loc4.w)) / l.w;\n" \
	"	alpha =alpha * c.w ;\n" \
	"	c = vec4(c.rgb*alpha, alpha);\n" \
	"\n" \
	"	//VOL_COLOR_OUTPUT\n" \
	"	c.xyz = c.xyz*clamp(1.0-base.loc1.x, 0.0, 1.0) + base.loc1.x*c.xyz*(base.loc1.y > 0.0?(n.w + n.z):1.0);\n" \
	"	c.xyz *= pow(1.0 - base.loc1.x/2.0, 2.0) + 1.0;\n" \
	"	c.rgb = c.rgb*base.loc6.z;\n" \
	"\n"
*/
#define VOL_INDEX_COLOR_D_BODY \
	"	//VOL_INDEX_COLOR_D_BODY\n" \
	"	vec4 v;\n" \
	"	uint label = uint(texture(tex0, t.stp).x*base.loc5.w+0.5); //get mask value\n" \
	"	vec4 c = texture(tex7, vec2((float(label%256)+0.5)/256.0, (float(label/256)+0.5)/256.0));\n" \
	"	c.rgb = c.rgb*base.loc6.z;\n" \
	"\n"

#define VOL_COLOR_OUTPUT \
	"	//VOL_COLOR_OUTPUT\n" \
	"	c.xyz = c.xyz*clamp(1.0-base.loc1.x, 0.0, 1.0) + base.loc1.x*c.xyz*(base.loc1.y > 0.0?(n.w + n.z):1.0);\n" \
	"	c.xyz *= pow(1.0 - base.loc1.x/2.0, 2.0) + 1.0;\n" \
	"\n"

#define VOL_FOG_BODY \
	"	//VOL_FOG_BODY\n" \
	"	v.x = (fp.z-fp.w)/(fp.z-fp.y);\n" \
	"	v.x = 1.0-clamp(v.x, 0.0, 1.0);\n" \
	"	v.x = 1.0-exp(-pow(v.x*2.5, 2.0));\n" \
	"	c.xyz = mix(c.xyz, vec3(0.0), v.x*fp.x); \n" \
	"\n"

#define VOL_RASTER_BLEND \
	"	//VOL_RASTER_BLEND\n" \
	"	FragColor = c*l.w; // VOL_RASTER_BLEND\n" \
	"\n"

#define VOL_RASTER_BLEND_SOLID \
	"	//VOL_RASTER_BLEND_SOLID\n" \
	"	FragColor = c;\n" \
	"\n"

#define VOL_RASTER_BLEND_ID \
	"	//VOL_RASTER_BLEND_ID\n" \
	"	FragColor = c*l.w;\n" \
	"\n"

#define VOL_RASTER_BLEND_DMAP \
	"	//VOL_RASTER_BLEND_DMAP\n" \
	"	//float prevz = texture(tex4, fcf).r;\n" \
	"	float currz = gl_FragCoord.z;\n" \
	"	float intpo = (c*l.w).r;\n" \
	"	//FragColor = vec4(vec3(intpo>0.05?currz:prevz), 1.0);\n" \
	"	if (intpo < 0.05) discard;\n" \
	"	FragColor = vec4(vec3(currz), 1.0);\n" \
	"\n"

#define VOL_RASTER_BLEND_NOMASK \
	"	//VOL_RASTER_BLEND_NOMASK\n" \
	"	vec4 cmask = texture(tex2, t.stp); //get mask value\n" \
	"	FragColor = vec4(1.0-cmask.x)*c*l.w;\n" \
	"\n"

#define VOL_RASTER_BLEND_NOMASK_SOLID \
	"	//VOL_RASTER_BLEND_NOMASK_SOLID\n" \
	"	vec4 cmask = texture(tex2, t.stp); //get mask value\n" \
	"	FragColor = vec4(1.0-cmask.x)*c;\n" \
	"\n"

#define VOL_RASTER_BLEND_NOMASK_ID \
	"	//VOL_RASTER_BLEND_NOMASK_ID\n" \
	"	vec4 cmask = texture(tex2, t.stp); //get mask value\n" \
	"	FragColor = vec4(1.0-cmask.x)*c*l.w;\n" \
	"\n"

#define VOL_RASTER_BLEND_NOMASK_DMAP \
	"	//VOL_RASTER_BLEND_NOMASK_DMAP\n" \
	"	float prevz = texture(tex4, fcf).r;\n" \
	"	float currz = gl_FragCoord.z;\n" \
	"	vec4 cmask = texture(tex2, t.stp); //get mask value\n" \
	"	float intpo = (vec4(1.0-cmask.x)*c*l.w).r;\n" \
	"	FragColor = vec4(vec3(intpo>0.05?currz:prevz), 1.0);\n" \
	"\n"

#define VOL_RASTER_BLEND_MASK \
	"	//VOL_RASTER_BLEND_MASK\n" \
	"	vec4 cmask = texture(tex2, t.stp); //get mask value\n" \
	"	FragColor = tf_alp*cmask.x<base.loc6.w?vec4(cmask.x*0.0002):vec4(cmask.x*0.0002)+vec4(cmask.x)*c*l.w;\n" \
	"	//FragColor = cmask;\n" \
	"\n"

#define VOL_RASTER_BLEND_MASK_SOLID \
	"	//VOL_RASTER_BLEND_MASK_SOLID\n" \
	"	vec4 cmask = texture(tex2, t.stp); //get mask value\n" \
	"	FragColor = tf_alp*cmask.x<base.loc6.w?vec4(0.0):vec4(cmask.x)*c;\n" \
	"\n"

#define VOL_RASTER_BLEND_MASK_ID \
	"	//VOL_RASTER_BLEND_MASK_ID\n" \
	"	vec4 cmask = texture(tex2, t.stp); //get mask value\n" \
	"	FragColor = vec4(cmask.x)*c*l.w;\n" \
	"\n"

#define VOL_RASTER_BLEND_MASK_DMAP \
	"	//VOL_RASTER_BLEND_MASK_DMAP\n" \
	"	float prevz = texture(tex4, fcf).r;\n" \
	"	float currz = gl_FragCoord.z;\n" \
	"	vec4 cmask = texture(tex2, t.stp); //get mask value\n" \
	"	float intpo = (vec4(cmask.x)*c*l.w).r;\n" \
	"	FragColor = vec4(vec3(intpo>0.05?currz:prevz), 1.0);\n" \
	"\n"

#define VOL_RASTER_BLEND_LABEL \
	"	//VOL_RASTER_BLEND_LABEL\n" \
	"	uint label = texture(tex3, t.stp).x; //get mask value\n" \
	"	vec4 sel = vec4(0.2,\n" \
	"					0.4,\n" \
	"					0.4, 1.0);\n" \
	"	float hue, p2, p3;\n" \
	"	if (label > uint(0))\n" \
	"	{\n" \
	"		hue = float(label%uint(360))/60.0;\n" \
	"		p2 = 1.0 - hue + floor(hue);\n" \
	"		p3 = hue - floor(hue);\n" \
	"		if (hue < 1.0)\n" \
	"			sel = vec4(1.0, p3, 1.0, 1.0);\n" \
	"		else if (hue < 2.0)\n" \
	"			sel = vec4(p2, 1.0, 1.0, 1.0);\n" \
	"		else if (hue < 3.0)\n" \
	"			sel = vec4(1.0, 1.0, p3, 1.0);\n" \
	"		else if (hue < 4.0)\n" \
	"			sel = vec4(1.0, p2, 1.0, 1.0);\n" \
	"		else if (hue < 5.0)\n" \
	"			sel = vec4(p3, 1.0, 1.0, 1.0);\n" \
	"		else\n" \
	"			sel = vec4(1.0, 1.0, p2, 1.0);\n" \
	"	}\n" \
	"	sel.xyz = sel.xyz*clamp(1.0-base.loc1.x, 0.0, 1.0) + base.loc1.x*sel.xyz*(base.loc1.y > 0.0?(n.w + n.z):1.0);\n" \
	"	sel.xyz *= pow(1.0 - base.loc1.x / 2.0, 2.0) + 1.0;\n" \
	"	FragColor = sel*l.w;\n" \
	"\n"

#define VOL_RASTER_BLEND_LABEL_MASK \
	"	//VOL_RASTER_BLEND_LABEL_MASK\n" \
	"	vec4 cmask = texture(tex2, t.stp); //get mask value\n" \
	"	if (cmask.x <= base.loc6.w)\n" \
	"	{\n" \
	"		FragColor = c*l.w;\n" \
	"		return;\n" \
	"	}\n" \
	"	uint label = texture(tex3, t.stp).x; //get mask value\n" \
	"	vec4 sel = vec4(0.1,\n" \
	"					0.2,\n" \
	"					0.2, 0.5);\n" \
	"	float hue, p2, p3;\n" \
	"	if (label > uint(0))\n" \
	"	{\n" \
	"		hue = float(label%uint(360))/60.0;\n" \
	"		p2 = 1.0 - hue + floor(hue);\n" \
	"		p3 = hue - floor(hue);\n" \
	"		if (hue < 1.0)\n" \
	"			sel = vec4(1.0, p3, 0.0, 1.0);\n" \
	"		else if (hue < 2.0)\n" \
	"			sel = vec4(p2, 1.0, 0.0, 1.0);\n" \
	"		else if (hue < 3.0)\n" \
	"			sel = vec4(0.0, 1.0, p3, 1.0);\n" \
	"		else if (hue < 4.0)\n" \
	"			sel = vec4(0.0, p2, 1.0, 1.0);\n" \
	"		else if (hue < 5.0)\n" \
	"			sel = vec4(p3, 0.0, 1.0, 1.0);\n" \
	"		else\n" \
	"			sel = vec4(1.0, 0.0, p2, 1.0);\n" \
	"	}\n" \
	"	sel.xyz = sel.xyz*clamp(1.0-base.loc1.x, 0.0, 1.0) + base.loc1.x*sel.xyz*(base.loc1.y > 0.0?(n.w + n.z):1.0);\n" \
	"	sel.xyz *= pow(1.0 - base.loc1.x / 2.0, 2.0) + 1.0;\n" \
	"	FragColor = sel*alpha*tf_alp*l.w;\n" \
	"\n"

}//namespace FLIVR
