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

#include <math.h>

#include <FLIVR/TextureBrick.h>
#include <FLIVR/TextureRenderer.h>
#include <FLIVR/Utils.h>

#include <utility>

#ifndef _UNIT_TEST_VOLUME_RENDERER_
#include <wx/wx.h>
#include <wx/url.h>
#include <iostream>
#include <jpeglib.h>
#include "../compatibility.h"
#include <setjmp.h>
#include <zlib.h>
#include <wx/stdpaths.h>
#include <h5j_reader.h>
//#include <lz4frame.h>
#include <lz4.h>
#include <blosc2.h>
#endif

#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/filter/zstd.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/device/array.hpp>

using namespace std;

namespace FLIVR
{

#ifndef _UNIT_TEST_VOLUME_RENDERER_
	CURL* TextureBrick::s_curl_ = NULL;
	CURL* TextureBrick::s_curlm_ = NULL;
#endif

	map<wstring, wstring> TextureBrick::cache_table_ = map<wstring, wstring>();
	map<wstring, MemCache*> TextureBrick::memcache_table_ = map<wstring, MemCache*>();
	list<std::wstring> TextureBrick::memcache_order = list<std::wstring>();
	size_t TextureBrick::memcache_size = 0;
	size_t TextureBrick::memcache_limit = 1024*1024*1024;

    
   TextureBrick::TextureBrick (const shared_ptr<VL_Nrrd>& n0, const shared_ptr<VL_Nrrd>& n1,
         int nx, int ny, int nz, int nc, int* nb,
         int ox, int oy, int oz,
         int mx, int my, int mz,
         const BBox& bbox, const BBox& tbox, const BBox& dbox, int findex, long long offset, long long fsize)
      : nx_(nx), ny_(ny), nz_(nz), nc_(nc), ox_(ox), oy_(oy), oz_(oz),
      mx_(mx), my_(my), mz_(mz), bbox_(bbox), tbox_(tbox), dbox_(dbox), findex_(findex), offset_(offset), fsize_(fsize)
   {
      for (int i=0; i<TEXTURE_MAX_COMPONENTS; i++)
      {
         data_[i] = 0;
         nb_[i] = 0;
         ntype_[i] = TYPE_NONE;
      }

      for (int c=0; c<nc_; c++)
      {
         nb_[c] = nb[c];
      }
      if (nc_==1)
      {
         ntype_[0] = TYPE_INT;
      }
      else if (nc_>1)
      {
         ntype_[0] = TYPE_INT_GRAD;
         ntype_[1] = TYPE_GM;
      }

	  compute_edge_rays(bbox_);
      compute_edge_rays_tex(tbox_);

      data_[0] = n0;
      data_[1] = n1;

      nmask_ = -1;
	  nlabel_ = -1;
	  nstroke_ = -1;

      //if it's been drawn in a full update loop
      for (int i=0; i<TEXTURE_RENDER_MODES; i++)
      {
         drawn_[i] = false;
         dirty_[i] = false;
         skip_[i] = false;
         modified_[i] = true;
      }

      //priority
      priority_ = 0;

	  brkdata_ = NULL;
	  id_in_loadedbrks = -1;
	  loading_ = false;
	  
	  disp_ = true;
	  prevent_tex_deletion_ = false;
	  lock_brickdata_ = false;

	  compression_ = false;
   }

   TextureBrick::~TextureBrick()
   {
      // Creator of the brick owns the nrrds.
      // This object never deletes that memory.
   }

   /* The cube is numbered in the following way

Corners:

2________6        y
/|        |        |
/ |       /|        |
/  |      / |        |
/   0_____/__4        |
3---------7   /        |_________ x
|  /      |  /         /
| /       | /         /
|/        |/         /
1_________5         /
z

Edges:

,____1___,        y
/|        |        |
/ 0       /|        |
9  |     10 2        |
/   |__3__/__|        |
/_____5___/   /        |_________ x
|  /      | 11         /
4 8       6 /         /
|/        |/         /
|____7____/         /
z
    */

   void TextureBrick::compute_edge_rays(BBox &bbox)
   {
      // set up vertices
      Point corner[8];
      corner[0] = bbox.min();
      corner[1] = Point(bbox.min().x(), bbox.min().y(), bbox.max().z());
      corner[2] = Point(bbox.min().x(), bbox.max().y(), bbox.min().z());
      corner[3] = Point(bbox.min().x(), bbox.max().y(), bbox.max().z());
      corner[4] = Point(bbox.max().x(), bbox.min().y(), bbox.min().z());
      corner[5] = Point(bbox.max().x(), bbox.min().y(), bbox.max().z());
      corner[6] = Point(bbox.max().x(), bbox.max().y(), bbox.min().z());
      corner[7] = bbox.max();

      // set up edges
/*
	  edge_[0] = Ray(corner[0], corner[2] - corner[0]);
      edge_[1] = Ray(corner[2], corner[6] - corner[2]);
      edge_[2] = Ray(corner[4], corner[6] - corner[4]);
      edge_[3] = Ray(corner[0], corner[4] - corner[0]);
      edge_[4] = Ray(corner[1], corner[3] - corner[1]);
      edge_[5] = Ray(corner[3], corner[7] - corner[3]);
      edge_[6] = Ray(corner[5], corner[7] - corner[5]);
      edge_[7] = Ray(corner[1], corner[5] - corner[1]);
      edge_[8] = Ray(corner[0], corner[1] - corner[0]);
      edge_[9] = Ray(corner[2], corner[3] - corner[2]);
      edge_[10] = Ray(corner[6], corner[7] - corner[6]);
      edge_[11] = Ray(corner[4], corner[5] - corner[4]);
*/
	  edge_[0] = Ray(corner[0], corner[2] - corner[0]);
	  edge_[1] = Ray(corner[4], corner[6] - corner[4]);
	  edge_[2] = Ray(corner[5], corner[7] - corner[5]);
	  edge_[3] = Ray(corner[1], corner[3] - corner[1]);
      edge_[4] = Ray(corner[2], corner[6] - corner[2]);
      edge_[5] = Ray(corner[0], corner[4] - corner[0]);
      edge_[6] = Ray(corner[3], corner[7] - corner[3]);
      edge_[7] = Ray(corner[1], corner[5] - corner[1]);
      edge_[8] = Ray(corner[0], corner[1] - corner[0]);
      edge_[9] = Ray(corner[2], corner[3] - corner[2]);
      edge_[10] = Ray(corner[6], corner[7] - corner[6]);
      edge_[11] = Ray(corner[4], corner[5] - corner[4]);
   }

   void TextureBrick::compute_edge_rays_tex(BBox &bbox)
   {
      // set up vertices
      Point corner[8];
      corner[0] = bbox.min();
      corner[1] = Point(bbox.min().x(), bbox.min().y(), bbox.max().z());
      corner[2] = Point(bbox.min().x(), bbox.max().y(), bbox.min().z());
      corner[3] = Point(bbox.min().x(), bbox.max().y(), bbox.max().z());
      corner[4] = Point(bbox.max().x(), bbox.min().y(), bbox.min().z());
      corner[5] = Point(bbox.max().x(), bbox.min().y(), bbox.max().z());
      corner[6] = Point(bbox.max().x(), bbox.max().y(), bbox.min().z());
      corner[7] = bbox.max();

      // set up edges
/*
      tex_edge_[0] = Ray(corner[0], corner[2] - corner[0]);
      tex_edge_[1] = Ray(corner[2], corner[6] - corner[2]);
      tex_edge_[2] = Ray(corner[4], corner[6] - corner[4]);
      tex_edge_[3] = Ray(corner[0], corner[4] - corner[0]);
      tex_edge_[4] = Ray(corner[1], corner[3] - corner[1]);
      tex_edge_[5] = Ray(corner[3], corner[7] - corner[3]);
      tex_edge_[6] = Ray(corner[5], corner[7] - corner[5]);
      tex_edge_[7] = Ray(corner[1], corner[5] - corner[1]);
      tex_edge_[8] = Ray(corner[0], corner[1] - corner[0]);
      tex_edge_[9] = Ray(corner[2], corner[3] - corner[2]);
      tex_edge_[10] = Ray(corner[6], corner[7] - corner[6]);
      tex_edge_[11] = Ray(corner[4], corner[5] - corner[4]);
*/
	  tex_edge_[0] = Ray(corner[0], corner[2] - corner[0]);
	  tex_edge_[1] = Ray(corner[4], corner[6] - corner[4]);
	  tex_edge_[2] = Ray(corner[5], corner[7] - corner[5]);
	  tex_edge_[3] = Ray(corner[1], corner[3] - corner[1]);
      tex_edge_[4] = Ray(corner[2], corner[6] - corner[2]);
      tex_edge_[5] = Ray(corner[0], corner[4] - corner[0]);
      tex_edge_[6] = Ray(corner[3], corner[7] - corner[3]);
      tex_edge_[7] = Ray(corner[1], corner[5] - corner[1]);
      tex_edge_[8] = Ray(corner[0], corner[1] - corner[0]);
      tex_edge_[9] = Ray(corner[2], corner[3] - corner[2]);
      tex_edge_[10] = Ray(corner[6], corner[7] - corner[6]);
      tex_edge_[11] = Ray(corner[4], corner[5] - corner[4]);
   }

   // compute polygon of edge plane intersections
   void TextureBrick::compute_polygon(Ray& view, double t,
         vector<double>& vertex, vector<double>& texcoord,
         vector<int>& size)
   {
      compute_polygons(view, t, t, 1.0, vertex, texcoord, size);
   }

   //set timin_, timax_, dt_ and vray_
   bool TextureBrick::compute_t_index_min_max(Ray& view, double dt)
   {
      Point corner[8];
      corner[0] = bbox_.min();
      corner[1] = Point(bbox_.min().x(), bbox_.min().y(), bbox_.max().z());
      corner[2] = Point(bbox_.min().x(), bbox_.max().y(), bbox_.min().z());
      corner[3] = Point(bbox_.min().x(), bbox_.max().y(), bbox_.max().z());
      corner[4] = Point(bbox_.max().x(), bbox_.min().y(), bbox_.min().z());
      corner[5] = Point(bbox_.max().x(), bbox_.min().y(), bbox_.max().z());
      corner[6] = Point(bbox_.max().x(), bbox_.max().y(), bbox_.min().z());
      corner[7] = bbox_.max();

      double tmin = Dot(corner[0] - view.origin(), view.direction());
      double tmax = tmin;
      int maxi = 0;
	  int mini = 0;
      for (int i=1; i<8; i++)
      {
         double t = Dot(corner[i] - view.origin(), view.direction());
         if (t < tmin) { mini = i; tmin = t; }
         if (t > tmax) { maxi = i; tmax = t; }
      }

	  bool order = TextureRenderer::get_update_order();
	  double tanchor;
	  tanchor = Dot(corner[mini] - view.origin(), view.direction());
	  timin_ = (int)floor(tanchor/dt)+1;
	  tanchor = Dot(corner[maxi] - view.origin(), view.direction());
	  timax_ = (int)floor(tanchor/dt);
	  dt_ = dt;
	  vray_ = view;

	  return (timax_ - timin_) >= 0 ? true : false;
   }

   void TextureBrick::compute_slicenum(Ray& view, double dt, double& r_tmax, double& r_tmin, unsigned int& r_slicenum)
   {
	   Point corner[8];
	   corner[0] = bbox_.min();
	   corner[1] = Point(bbox_.min().x(), bbox_.min().y(), bbox_.max().z());
	   corner[2] = Point(bbox_.min().x(), bbox_.max().y(), bbox_.min().z());
	   corner[3] = Point(bbox_.min().x(), bbox_.max().y(), bbox_.max().z());
	   corner[4] = Point(bbox_.max().x(), bbox_.min().y(), bbox_.min().z());
	   corner[5] = Point(bbox_.max().x(), bbox_.min().y(), bbox_.max().z());
	   corner[6] = Point(bbox_.max().x(), bbox_.max().y(), bbox_.min().z());
	   corner[7] = bbox_.max();

	   double tmin = Dot(corner[0] - view.origin(), view.direction());
	   double tmax = tmin;
	   int maxi = 0;
	   int mini = 0;
	   for (int i = 1; i < 8; i++)
	   {
		   double t = Dot(corner[i] - view.origin(), view.direction());
		   if (t < tmin) { mini = i; tmin = t; }
		   if (t > tmax) { maxi = i; tmax = t; }
	   }

	   bool order = TextureRenderer::get_update_order();
	   double tanchor = Dot(corner[order ? mini : maxi], view.direction());
	   double tanchor0 = (floor(tanchor / dt) + 1) * dt;
	   double tanchordiff = tanchor - tanchor0;
	   if (order) tmin -= tanchordiff;
	   else tmax -= tanchordiff;

	   r_tmax = tmax;
	   r_tmin = tmin;
	   r_slicenum = (unsigned int)( (tmax-tmin) / dt );
   }

   void TextureBrick::compute_polygons(Ray& view, double dt,
         vector<double>& vertex, vector<double>& texcoord,
         vector<int>& size)
   {
      Point corner[8];
      corner[0] = bbox_.min();
      corner[1] = Point(bbox_.min().x(), bbox_.min().y(), bbox_.max().z());
      corner[2] = Point(bbox_.min().x(), bbox_.max().y(), bbox_.min().z());
      corner[3] = Point(bbox_.min().x(), bbox_.max().y(), bbox_.max().z());
      corner[4] = Point(bbox_.max().x(), bbox_.min().y(), bbox_.min().z());
      corner[5] = Point(bbox_.max().x(), bbox_.min().y(), bbox_.max().z());
      corner[6] = Point(bbox_.max().x(), bbox_.max().y(), bbox_.min().z());
      corner[7] = bbox_.max();

      double tmin = Dot(corner[0] - view.origin(), view.direction());
      double tmax = tmin;
      int maxi = 0;
	  int mini = 0;
      for (int i=1; i<8; i++)
      {
         double t = Dot(corner[i] - view.origin(), view.direction());
         if (t < tmin) { mini = i; tmin = t; }
         if (t > tmax) { maxi = i; tmax = t; }
      }

      // Make all of the slices consistent by offsetting them to a fixed
      // position in space (the origin).  This way they are consistent
      // between bricks and don't change with camera zoom.
/*      double tanchor = Dot(corner[maxi], view.direction());
      double tanchor0 = floor(tanchor/dt)*dt;
      double tanchordiff = tanchor - tanchor0;
      tmax -= tanchordiff;
*/
	  bool order = TextureRenderer::get_update_order();
	  double tanchor = Dot(corner[order?mini:maxi], view.direction());
	  double tanchor0 = (floor(tanchor/dt)+1)*dt;
	  double tanchordiff = tanchor - tanchor0;
	  if (order) tmin -= tanchordiff;
	  else tmax -= tanchordiff;

      compute_polygons(view, tmin, tmax, dt, vertex, texcoord, size);
   }

   // compute polygon list of edge plane intersections
   //
   // This is never called externally and could be private.
   //
   // The representation returned is not efficient, but it appears a
   // typical rendering only contains about 1k triangles.
   void TextureBrick::compute_polygons(Ray& view,
         double tmin, double tmax, double dt,
         vector<double>& vertex, vector<double>& texcoord,
         vector<int>& size)
   {
      Vector vv[12], tt[12]; // temp storage for vertices and texcoords

      int k = 0, degree = 0;

      // find up and right vectors
      Vector vdir = view.direction();
      view_vector_ = vdir;
      Vector up;
      Vector right;
      switch(MinIndex(fabs(vdir.x()),
               fabs(vdir.y()),
               fabs(vdir.z())))
      {
      case 0:
         up.x(0.0); up.y(-vdir.z()); up.z(vdir.y());
         break;
      case 1:
         up.x(-vdir.z()); up.y(0.0); up.z(vdir.x());
         break;
      case 2:
         up.x(-vdir.y()); up.y(vdir.x()); up.z(0.0);
         break;
      }
      up.normalize();
      right = Cross(vdir, up);
      bool order = TextureRenderer::get_update_order();
      for (double t = order?tmin:tmax;
            order?(t <= tmax):(t >= tmin);
            t += order?dt:-dt)
      {
         // we compute polys back to front
         // find intersections
         degree = 0;
		 FLIVR::Vector vec = -view.direction();
         FLIVR::Point pnt = view.parameter(t);
		 //�f�ʂ̒��_��bbox�̒��_�Əd�Ȃ����Ƃ��d����h��
         for (size_t j=0; j<=3; j++)
         {
            double u;
            bool intersects = edge_[j].planeIntersectParameter
               (vec, pnt, u);
            if (intersects && u >= 0.0 && u <= 1.0)
            {
               Point p;
               p = edge_[j].parameter(u);
               vv[degree] = (Vector)p;
               p = tex_edge_[j].parameter(u);
               tt[degree] = (Vector)p;
               degree++;
            }
         }
		 for (size_t j=4; j<=11; j++)
         {
            double u;
            bool intersects = edge_[j].planeIntersectParameter
               (vec, pnt, u);
            if (intersects && u > 0.0 && u < 1.0)
            {
               Point p;
               p = edge_[j].parameter(u);
               vv[degree] = (Vector)p;
               p = tex_edge_[j].parameter(u);
               tt[degree] = (Vector)p;
               degree++;
            }
         }

         if (degree < 3 || degree >6)
			 continue;

         if (degree > 3)
         {
            // compute centroids
            Vector vc(0.0, 0.0, 0.0), tc(0.0, 0.0, 0.0);
            for (int j=0; j<degree; j++)
            {
               vc += vv[j]; tc += tt[j];
            }
            vc /= (double)degree; tc /= (double)degree;

            // sort vertices
            int idx[6];
            double pa[6];
            for (int i=0; i<degree; i++)
            {
               double vx = Dot(vv[i] - vc, right);
               double vy = Dot(vv[i] - vc, up);

               // compute pseudo-angle
               pa[i] = vy / (fabs(vx) + fabs(vy));
               if (vx < 0.0) pa[i] = 2.0 - pa[i];
               else if (vy < 0.0) pa[i] = 4.0 + pa[i];
               // init idx
               idx[i] = i;
            }
            Sort(pa, idx, degree);

            // output polygon
            for (int j=0; j<degree; j++)
            {
               vertex.push_back(vv[idx[j]].x());
               vertex.push_back(vv[idx[j]].y());
               vertex.push_back(vv[idx[j]].z());
               texcoord.push_back(tt[idx[j]].x());
               texcoord.push_back(tt[idx[j]].y());
               texcoord.push_back(tt[idx[j]].z());
            }
         }
         else if (degree == 3)
         {
            // output a single triangle
            for (int j=0; j<degree; j++)
            {
               vertex.push_back(vv[j].x());
               vertex.push_back(vv[j].y());
               vertex.push_back(vv[j].z());
               texcoord.push_back(tt[j].x());
               texcoord.push_back(tt[j].y());
               texcoord.push_back(tt[j].z());
            }
         }
         k += degree;
         size.push_back(degree);
      }
   }

   // compute polygon of edge plane intersections
   void TextureBrick::compute_polygons(Ray& view, double dt,
         vector<float>& vertex, vector<uint32_t>& index,
         vector<uint32_t>& size)
   {
      Point corner[8];
      corner[0] = bbox_.min();
      corner[1] = Point(bbox_.min().x(), bbox_.min().y(), bbox_.max().z());
      corner[2] = Point(bbox_.min().x(), bbox_.max().y(), bbox_.min().z());
      corner[3] = Point(bbox_.min().x(), bbox_.max().y(), bbox_.max().z());
      corner[4] = Point(bbox_.max().x(), bbox_.min().y(), bbox_.min().z());
      corner[5] = Point(bbox_.max().x(), bbox_.min().y(), bbox_.max().z());
      corner[6] = Point(bbox_.max().x(), bbox_.max().y(), bbox_.min().z());
      corner[7] = bbox_.max();

      double tmin = Dot(corner[0] - view.origin(), view.direction());
      double tmax = tmin;
      int maxi = 0;
	  int mini = 0;
      for (int i=1; i<8; i++)
      {
         double t = Dot(corner[i] - view.origin(), view.direction());
         if (t < tmin) { mini = i; tmin = t; }
         if (t > tmax) { maxi = i; tmax = t; }
      }

      bool order = TextureRenderer::get_update_order();
	  double tanchor = Dot(corner[order?mini:maxi], view.direction());
	  double tanchor0 = (floor(tanchor/dt)+1)*dt;
	  double tanchordiff = tanchor - tanchor0;
	  if (order) tmin -= tanchordiff;
	  else tmax -= tanchordiff;

      compute_polygons(view, tmin, tmax, dt, vertex, index, size);
   }

   // compute polygon list of edge plane intersections
   //
   // This is never called externally and could be private.
   //
   // The representation returned is not efficient, but it appears a
   // typical rendering only contains about 1k triangles.
   void TextureBrick::compute_polygons(Ray& view,
         double tmin, double tmax, double dt,
         vector<float>& vertex, vector<uint32_t>& index,
		 vector<uint32_t>& size)
   {
      Vector vv[12], tt[12]; // temp storage for vertices and texcoords

      uint32_t degree = 0;

      // find up and right vectors
      Vector vdir = view.direction();
      view_vector_ = vdir;
      Vector up;
      Vector right;
      switch(MinIndex(fabs(vdir.x()),
               fabs(vdir.y()),
               fabs(vdir.z())))
      {
      case 0:
         up.x(0.0); up.y(-vdir.z()); up.z(vdir.y());
         break;
      case 1:
         up.x(-vdir.z()); up.y(0.0); up.z(vdir.x());
         break;
      case 2:
         up.x(-vdir.y()); up.y(vdir.x()); up.z(0.0);
         break;
      }
      up.normalize();
      right = Cross(vdir, up);
      bool order = TextureRenderer::get_update_order();
	  size_t vert_count = 0;
      for (double t = order?tmin:tmax;
            order?(t <= tmax):(t >= tmin);
            t += order?dt:-dt)
	  {
		  // we compute polys back to front
		  // find intersections
		  degree = 0;
		  double u;

		  FLIVR::Vector vec = -view.direction();
		  FLIVR::Point pnt = view.parameter(t);
		  //�f�ʂ̒��_��bbox�̒��_�Əd�Ȃ����Ƃ��d����h��
		  for (size_t j=0; j<=3; j++)
		  {
			  double u;
			  bool intersects = edge_[j].planeIntersectParameter
				  (vec, pnt, u);
			  if (intersects && u >= 0.0 && u <= 1.0)
			  {
				  Point p;
				  p = edge_[j].parameter(u);
				  vv[degree] = (Vector)p;
				  p = tex_edge_[j].parameter(u);
				  tt[degree] = (Vector)p;
				  degree++;
			  }
		  }
		  for (size_t j=4; j<=11; j++)
		  {
			  double u;
			  bool intersects = edge_[j].planeIntersectParameter
				  (vec, pnt, u);
			  if (intersects && u > 0.0 && u < 1.0)
			  {
				  Point p;
				  p = edge_[j].parameter(u);
				  vv[degree] = (Vector)p;
				  p = tex_edge_[j].parameter(u);
				  tt[degree] = (Vector)p;
				  degree++;
			  }
		  }
		  
         if (degree < 3 || degree >6) continue;
		 bool sorted = degree > 3;
		 uint32_t idx[6];
		 if (sorted) {
			// compute centroids
			Vector vc(0.0, 0.0, 0.0), tc(0.0, 0.0, 0.0);
			for (int j=0; j<degree; j++)
			{
				vc += vv[j]; tc += tt[j];
			}
			vc /= (double)degree; tc /= (double)degree;

			// sort vertices
			double pa[6];
			for (uint32_t i=0; i<degree; i++)
			{
				double vx = Dot(vv[i] - vc, right);
				double vy = Dot(vv[i] - vc, up);

				// compute pseudo-angle
				pa[i] = vy / (fabs(vx) + fabs(vy));
				if (vx < 0.0) pa[i] = 2.0 - pa[i];
				else if (vy < 0.0) pa[i] = 4.0 + pa[i];
				// init idx
				idx[i] = i;
			}
			Sort(pa, idx, degree);
		 }
        // save all of the indices
		for(uint32_t j = 1; j < degree - 1; j++) {
			index.push_back(vert_count);
			index.push_back(vert_count+j);
			index.push_back(vert_count+j+1);
		}
        // save all of the verts
        for (uint32_t j=0; j<degree; j++)
        {
            vertex.push_back((sorted?vv[idx[j]]:vv[j]).x());
            vertex.push_back((sorted?vv[idx[j]]:vv[j]).y());
            vertex.push_back((sorted?vv[idx[j]]:vv[j]).z());
            vertex.push_back((sorted?tt[idx[j]]:tt[j]).x());
            vertex.push_back((sorted?tt[idx[j]]:tt[j]).y());
            vertex.push_back((sorted?tt[idx[j]]:tt[j]).z());
			vert_count++;
        }

		size.push_back(degree);
      }
   }

   void TextureBrick::get_polygon(int tid, int &size_v, float* &v, int &size_i, uint32_t* &i)
   {
	   if (size_i_.empty() || size_integ_i_.empty() || vertex_.empty() || index_.empty())
	   {
		   size_v = 0;
		   size_i = 0;
		   v = nullptr;
		   i = nullptr;

		   return;
	   }

	   bool order = TextureRenderer::get_update_order();
	   int id = (order ? tid - timin_ : timax_ - tid);

	   if (id < 0 || id >= size_integ_v_.size() || id >= size_integ_i_.size())
	   {
		   size_v = 0;
		   size_i = 0;
		   v = nullptr;
		   i = nullptr;

		   return;
	   }

	   unsigned int offset_v = size_integ_v_[id];
	   unsigned int offset_i = size_integ_i_[id];
	   
	   size_v = size_v_[id];
	   size_i = size_i_[id];
	   if (size_i > 0 && size_v >= 3)
	   {
		   v = &vertex_[offset_v*6];
		   i = &index_[offset_i*3];
	   }
	   else
	   {
		   size_v = 0;
		   size_i = 0;
		   v = nullptr;
		   i = nullptr;
	   }
   }
   
   void TextureBrick::clear_polygons()
   {
	   if (!vertex_.empty()) vertex_.clear();
	   if (!index_.empty()) index_.clear();
	   if (!size_v_.empty()) size_v_.clear();
	   if (!size_integ_v_.empty()) size_integ_v_.clear();
	   if (!size_i_.empty()) size_i_.clear();
	   if (!size_integ_i_.empty()) size_integ_i_.clear();
   }

   void TextureBrick::compute_polygons2()
   {
	  clear_polygons();

      Vector vv[12], tt[12]; // temp storage for vertices and texcoords

      int k = 0, l = 0, degree = 0;

      // find up and right vectors
      Vector vdir = vray_.direction();
      view_vector_ = vdir;
      Vector up;
      Vector right;
      switch(MinIndex(fabs(vdir.x()),
               fabs(vdir.y()),
               fabs(vdir.z())))
      {
      case 0:
         up.x(0.0); up.y(-vdir.z()); up.z(vdir.y());
         break;
      case 1:
         up.x(-vdir.z()); up.y(0.0); up.z(vdir.x());
         break;
      case 2:
         up.x(-vdir.y()); up.y(vdir.x()); up.z(0.0);
         break;
      }
      up.normalize();
      right = Cross(vdir, up);
      bool order = TextureRenderer::get_update_order();
	  size_t vert_count = 0;
      for (int t = order?timin_:timax_;
            order?(t <= timax_):(t >= timin_);
            t += order?1:-1)
      {
         // we compute polys back to front
         // find intersections
         degree = 0;
         FLIVR::Vector vec = -vray_.direction();
         FLIVR::Point pnt = vray_.parameter(t*dt_);
		 //�f�ʂ̒��_��bbox�̒��_�Əd�Ȃ����Ƃ��d����h��
         for (size_t j=0; j<=3; j++)
         {
            double u;
            bool intersects = edge_[j].planeIntersectParameter
               (vec, pnt, u);
            if (intersects && u >= 0.0 && u <= 1.0)
            {
               Point p;
               p = edge_[j].parameter(u);
               vv[degree] = (Vector)p;
               p = tex_edge_[j].parameter(u);
               tt[degree] = (Vector)p;
               degree++;
            }
         }
		 for (size_t j=4; j<=11; j++)
         {
            double u;
            bool intersects = edge_[j].planeIntersectParameter
               (vec, pnt, u);
            if (intersects && u > 0.0 && u < 1.0)
            {
               Point p;
               p = edge_[j].parameter(u);
               vv[degree] = (Vector)p;
               p = tex_edge_[j].parameter(u);
               tt[degree] = (Vector)p;
               degree++;
            }
         }

		 if (degree > 3 && degree <= 6)
		 {
			 bool sorted = degree > 3;
			 uint32_t idx[6];
			 if (sorted) {
				 // compute centroids
				 Vector vc(0.0, 0.0, 0.0), tc(0.0, 0.0, 0.0);
				 for (int j=0; j<degree; j++)
				 {
					 vc += vv[j]; tc += tt[j];
				 }
				 vc /= (double)degree; tc /= (double)degree;

				 // sort vertices
				 double pa[6];
				 for (uint32_t i=0; i<degree; i++)
				 {
					 double vx = Dot(vv[i] - vc, right);
					 double vy = Dot(vv[i] - vc, up);

					 // compute pseudo-angle
					 pa[i] = vy / (fabs(vx) + fabs(vy));
					 if (vx < 0.0) pa[i] = 2.0 - pa[i];
					 else if (vy < 0.0) pa[i] = 4.0 + pa[i];
					 // init idx
					 idx[i] = i;
				 }
				 Sort(pa, idx, degree);
			 }
			 // save all of the indices
			 for(uint32_t j = 1; j < degree - 1; j++) {
				 index_.push_back(0);
				 index_.push_back(j);
				 index_.push_back(j+1);
			 }
			 // save all of the verts
			 for (uint32_t j=0; j<degree; j++)
			 {
				 vertex_.push_back((sorted?vv[idx[j]]:vv[j]).x());
				 vertex_.push_back((sorted?vv[idx[j]]:vv[j]).y());
				 vertex_.push_back((sorted?vv[idx[j]]:vv[j]).z());
				 vertex_.push_back((sorted?tt[idx[j]]:tt[j]).x());
				 vertex_.push_back((sorted?tt[idx[j]]:tt[j]).y());
				 vertex_.push_back((sorted?tt[idx[j]]:tt[j]).z());
				 vert_count++;
			 }
		 }
		 else
		 {
			 for(uint32_t j = 1; j < degree - 1; j++) {
				 index_.push_back(0);
				 index_.push_back(j);
				 index_.push_back(j+1);
			 }
			 // save all of the verts
			 for (uint32_t j=0; j<degree; j++)
			 {
				 vertex_.push_back(vv[j].x());
				 vertex_.push_back(vv[j].y());
				 vertex_.push_back(vv[j].z());
				 vertex_.push_back(tt[j].x());
				 vertex_.push_back(tt[j].y());
				 vertex_.push_back(tt[j].z());
				 vert_count++;
			 }
		 }

		size_v_.push_back(degree);
		size_i_.push_back((degree-2 > 0) ? degree-2 : 0);
		size_integ_v_.push_back(l);
		size_integ_i_.push_back(k);
		l += degree;
		k += degree-2;
	  }
   }

   int TextureBrick::sx()
   {
	   if (!data_[0]->getNrrd())
		   return 0;
	   if (data_[0]->getNrrd()->dim == 3)
		   return (int)data_[0]->getNrrd()->axis[0].size;
	   else
		   return (int)data_[0]->getNrrd()->axis[1].size;
   }

   int TextureBrick::sy()
   {
	   if (!data_[0]->getNrrd())
		   return 0;
	   if (data_[0]->getNrrd()->dim == 3)
		   return (int)data_[0]->getNrrd()->axis[1].size;
	   else
		   return (int)data_[0]->getNrrd()->axis[2].size;
   }

   int TextureBrick::sz()
   {
	   if (!data_[0]->getNrrd())
		   return 0;
	   if (data_[0]->getNrrd()->dim == 3)
		   return (int)data_[0]->getNrrd()->axis[2].size;
	   else
		   return (int)data_[0]->getNrrd()->axis[3].size;
   }

   VkFormat TextureBrick::tex_format_aux(Nrrd* n)
   {
      if (n->type == nrrdTypeChar)   return compression_ ? VK_FORMAT_BC4_SNORM_BLOCK : VK_FORMAT_R8_SNORM;
      if (n->type == nrrdTypeUChar)  return compression_ ? VK_FORMAT_BC4_UNORM_BLOCK : VK_FORMAT_R8_UNORM;
      if (n->type == nrrdTypeShort)  return VK_FORMAT_R16_SNORM;
      if (n->type == nrrdTypeUShort) return VK_FORMAT_R16_UNORM;
	  if (n->type == nrrdTypeInt)    return VK_FORMAT_R32_SINT;
      if (n->type == nrrdTypeUInt)   return VK_FORMAT_R32_UINT;
	  if (n->type == nrrdTypeFloat)  return VK_FORMAT_R32_SFLOAT;
      return VK_FORMAT_UNDEFINED;
   }

   size_t TextureBrick::tex_format_size(VkFormat t)
   {
      if (t == VK_FORMAT_R8_SNORM || t == VK_FORMAT_BC4_SNORM_BLOCK)  { return sizeof(int8_t); }
      if (t == VK_FORMAT_R8_UNORM || t == VK_FORMAT_BC4_UNORM_BLOCK || t == VK_FORMAT_R8_UINT)  { return sizeof(uint8_t); }
      if (t == VK_FORMAT_R16_SNORM) { return sizeof(int16_t); }
      if (t == VK_FORMAT_R16_UNORM || t == VK_FORMAT_R16_UINT) { return sizeof(uint16_t); }
      if (t == VK_FORMAT_R32_SINT)  { return sizeof(int32_t); }
      if (t == VK_FORMAT_R32_UINT)  { return sizeof(uint32_t); }
      if (t == VK_FORMAT_R32_SFLOAT){ return sizeof(float); }
      return 0;
   }

   VkFormat TextureBrick::tex_format(int c)
   {
      if (c < nc_)
         return tex_format_aux(data_[c]->getNrrd());
      else if (c == nmask_)
		 return VK_FORMAT_R8_UNORM;
	  else if (c == nlabel_)
	  {
		  if (data_[c]->getNrrd()->type == nrrdTypeUChar)
			  return VK_FORMAT_R8_UINT;
		  else if (data_[c]->getNrrd()->type == nrrdTypeUShort)
			  return VK_FORMAT_R16_UINT;
		  else if (data_[c]->getNrrd()->type == nrrdTypeUInt)
			  return VK_FORMAT_R32_UINT;
	  }
	  else if (c == nstroke_)
         return VK_FORMAT_R8_UNORM;
      else
         return VK_FORMAT_UNDEFINED;
   }

   void *TextureBrick::tex_data(int c)
   {
	   if (c >= 0 && data_[c] && data_[c]->getNrrd())
	   {
		   if(data_[c]->getNrrd()->data)
		   {
			   char *ptr = (char *)(data_[c]->getNrrd()->data);
			   long long offset = (long long)(oz()) * (long long)(sx()) * (long long)(sy()) +
								  (long long)(oy()) * (long long)(sx()) +
								  (long long)(ox());
			   return ptr + offset * tex_format_size(tex_format(c));
		   }
	   }

	   return NULL;
   }

   void *TextureBrick::tex_data_brk(int c, const FileLocInfo* finfo)
   {
	   char *ptr = NULL;
	   if(brkdata_) ptr = (char *)(brkdata_->getData());
	   else
	   {
		   int bd = tex_format_size(tex_format(c));
		   size_t bsize = (size_t)nx_*(size_t)ny_*(size_t)nz_*(size_t)bd;
		   ptr = new char[bsize];
		   if (!read_brick(ptr, bsize, finfo))
		   {
			   delete [] ptr;
			   return NULL;
		   }
		   brkdata_ = make_shared<VL_Array>(ptr, bsize);
	   }
	   return ptr;
   }
   
   void TextureBrick::set_priority()
   {
      if (!data_[0] && !data_[0]->getNrrd())
      {
         priority_ = 0;
         return;
      }
      size_t vs = tex_format_size(tex_format(0));
      size_t sx = data_[0]->getNrrd()->axis[0].size;
      size_t sy = data_[0]->getNrrd()->axis[1].size;
      if (vs == 1)
      {
         unsigned char max = 0;
         unsigned char *ptr = (unsigned char *)(data_[0]->getNrrd()->data);
         for (int i=0; i<nx_; i++)
            for (int j=0; j<ny_; j++)
               for (int k=0; k<nz_; k++)
               {
                  long long index = (long long)(sx)* (long long)(sy) *
                     (long long)(oz_+k) + (long long)(sx) *
                     (long long)(oy_+j) + (long long)(ox_+i);
                  max = ptr[index]>max?ptr[index]:max;
               }
         if (max == 0)
            priority_ = 1;
         else
            priority_ = 0;
      }
      else if (vs == 2)
      {
         unsigned short max = 0;
         unsigned short *ptr = (unsigned short *)(data_[0]->getNrrd()->data);
         for (int i=0; i<nx_; i++)
            for (int j=0; j<ny_; j++)
               for (int k=0; k<nz_; k++)
               {
                  long long index = (long long)(sx)* (long long)(sy) *
                     (long long)(oz_+k) + (long long)(sx) *
                     (long long)(oy_+j) + (long long)(ox_+i);
                  max = ptr[index]>max?ptr[index]:max;
               }
         if (max == 0)
            priority_ = 1;
         else
            priority_ = 0;
	  }
   }

   
   void TextureBrick::set_priority_brk(ifstream* ifs, int filetype)
   {
/*	   if (!data_[0])
      {
         priority_ = 0;
         return;
      }
      
	  size_t vs = tex_type_size(tex_type(0));
      if (vs == 1)
      {
         unsigned char max = 0;
         unsigned char *ptr = NULL;
		 if (brkdata_) ptr = (unsigned char *)(brkdata_);
		 else {
			 ptr = new unsigned char[nx_*ny_*nz_];
			 if (!read_brick((char *)ptr, nx_*ny_*nz_*vs, ifs, filetype))
			 {
				 delete [] ptr;
				 priority_ = 0;
				 return;
			 }
		 }
         for (int i=0; i<nx_; i++)
            for (int j=0; j<ny_; j++)
               for (int k=0; k<nz_; k++)
               {
                  long long index = (long long)(k) * (long long)(ny_) * (long long)(nx_) +
									(long long)(j) * (long long)(nx_) + 
									(long long)(i);
                  max = ptr[index]>max?ptr[index]:max;
               }
         if (max == 0)
            priority_ = 1;
         else
            priority_ = 0;

		 if (!data_[0]->data) delete [] ptr;
      }
      else if (vs == 2)
      {
         unsigned short max = 0;
         unsigned short *ptr;
		 if (brkdata_) ptr = (unsigned short *)(brkdata_);
		 else {
			 ptr = new unsigned short[nx_*ny_*nz_];
			 if (!read_brick((char *)ptr, nx_*ny_*nz_*vs, ifs, filetype))
			 {
				 delete [] ptr;
				 priority_ = 0;
				 return;
			 }
		 }
         for (int i=0; i<nx_; i++)
            for (int j=0; j<ny_; j++)
               for (int k=0; k<nz_; k++)
               {
                  long long index = (long long)(k) * (long long)(ny_) * (long long)(nx_) +
									(long long)(j) * (long long)(nx_) + 
									(long long)(i);
                  max = ptr[index]>max?ptr[index]:max;
               }
         if (max == 0)
            priority_ = 1;
         else
            priority_ = 0;
		 
		 if (!brkdata_) delete [] ptr;
      }
*/   }

   void TextureBrick::freeBrkData()
   {
	   if (brkdata_) brkdata_.reset();
   }

   bool TextureBrick::read_brick(char* data, size_t size, const FileLocInfo* finfo)
   {
	   bool result = false;
#ifndef _UNIT_TEST_VOLUME_RENDERER_
	   if (!finfo || !finfo->isvalid) return result;

	   FileLocInfo tmpinfo = *finfo;
	   char *tmp = NULL;
	   size_t tmpsize;
	   read_brick_without_decomp(tmp, tmpsize, &tmpinfo);
	   if (finfo->type == BRICK_FILE_TYPE_RAW)
       {
           data = tmp;
           size = tmpsize;
           return true;
       }
	   else
           result = decompress_brick(data, tmp, size, tmpsize, finfo->type, nx(), ny(), nz(), nb(0), finfo->blosc_blocksize_x, finfo->blosc_blocksize_y, finfo->blosc_blocksize_z);

	   if (tmp)
		   delete[] tmp;
#endif
	   return result;
   }

#ifndef _UNIT_TEST_VOLUME_RENDERER_

#define DOWNLOAD_BUFSIZE 8192
   
   struct MemoryStruct {
	   char *memory;
	   size_t size;
   };
   
   size_t TextureBrick::WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
   {
	   size_t realsize = size * nmemb;
	   struct MemoryStruct *mem = (struct MemoryStruct *)userp;

	   mem->memory = (char *)realloc(mem->memory, mem->size + realsize + 1);
	   if(mem->memory == NULL) {
		   /* out of memory! */ 
		   printf("not enough memory (realloc returned NULL)\n");
		   return 0;
	   }

	   memcpy(&(mem->memory[mem->size]), contents, realsize);
	   mem->size += realsize;
	   mem->memory[mem->size] = 0;

	   return realsize;
   }

   size_t TextureBrick::WriteFileCallback(void *contents, size_t size, size_t nmemb, void *userp)
   {
	   ofstream *out = static_cast<std::ofstream *>(userp);
	   size_t nbytes = size * nmemb;
	   out->write((char *)contents, nbytes);
	   return nbytes;
   }

   int TextureBrick::xferinfo(void *p, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
   {
	   wxThread *wxth = (wxThread *)p;
	   if (wxth && wxth->TestDestroy())
		   return 1;
	   return 0;
   }

   bool TextureBrick::read_brick_without_decomp(char* &data, size_t &readsize, FileLocInfo* finfo, wxThread *th)
   {
	   readsize = -1;

	   if (!finfo || !finfo->isvalid) return false;

	   if (finfo->type == BRICK_FILE_TYPE_H265) {
		   auto itr = memcache_table_.find(finfo->filename);
		   if (itr != memcache_table_.end())
		   {
			   char *ptr = itr->second->data;
			   size_t fsize = itr->second->datasize;
			   size_t h265size = finfo->datasize;
			   if (h265size <= 0) h265size = fsize;
			   data = new char[h265size];
			   memcpy(data, ptr+finfo->offset, h265size);
			   readsize = h265size;
			   return true;
		   }
	   }
	   
	   if (finfo->isurl)
	   {
		   bool found_cache = false;
		   wxString wxcfname = finfo->cache_filename;
		   if (finfo->cached && wxFileExists(wxcfname))
			   found_cache = true;
		   else
		   {
			   wstring fcname;

			   auto itr = cache_table_.find(fcname);
			   if (itr != cache_table_.end())
			   {
				   wxcfname = itr->second;
				   if (wxFileExists(wxcfname))
						found_cache = true;
			   }

			   if (found_cache)
			   {
				   finfo->cached = true;
				   finfo->cache_filename = fcname;
			   }
		   }

		   if (!found_cache)
		   {
			   CURLcode ret;

			   if (s_curl_ == NULL) {
				   cerr << "curl_easy_init() failed" << endl;
				   return false;
			   }
			   curl_easy_reset(s_curl_);
			   curl_easy_setopt(s_curl_, CURLOPT_URL, wxString(finfo->filename).ToStdString().c_str());
               curl_easy_setopt(s_curl_, CURLOPT_TIMEOUT, 10L);
			   curl_easy_setopt(s_curl_, CURLOPT_USERAGENT, "libcurl-agent/1.0");
			   curl_easy_setopt(s_curl_, CURLOPT_WRITEFUNCTION, WriteFileCallback);
			   curl_easy_setopt(s_curl_, CURLOPT_SSL_VERIFYPEER, 0);
			   curl_easy_setopt(s_curl_, CURLOPT_NOSIGNAL, 1);
			   curl_easy_setopt(s_curl_, CURLOPT_FAILONERROR, 1);
			   curl_easy_setopt(s_curl_, CURLOPT_FILETIME, 1);
			   curl_easy_setopt(s_curl_, CURLOPT_XFERINFOFUNCTION, xferinfo);
			   curl_easy_setopt(s_curl_, CURLOPT_XFERINFODATA, th);
			   curl_easy_setopt(s_curl_, CURLOPT_NOPROGRESS, 0L);

			   wxString expath = wxStandardPaths::Get().GetExecutablePath();
			   expath = expath.BeforeLast(GETSLASH(),NULL);
#ifdef _DARWIN
			   wxString dft = expath + "/../Resources/vvd_cache";
			   if (!wxDirExists(dft))
				   wxMkdir(dft);
			   dft += L"/";
#else
			   wxString dft = expath + GETSLASHS() + "vvd_cache";
			   wxString dft2 = wxStandardPaths::Get().GetUserDataDir() + GETSLASHS() + "vvd_cache";
			   if (!wxDirExists(dft) && wxDirExists(dft2))
				   dft = dft2;
			   else if (!wxDirExists(dft))
				   wxMkdir(dft);
			   dft += GETSLASH();
#endif
			   wstring randname;
			   int len = 16;
			   char s[17];
			   wxString test;
			   do {
				   const char alphanum[] =
					   "0123456789"
					   "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
					   "abcdefghijklmnopqrstuvwxyz";

				   for (int i = 0; i < len; ++i) {
					   s[i] = alphanum[rand() % (sizeof(alphanum) - 1)];
				   }
				   s[len] = 0;
				   test = dft;
				   test += s;
			   } while (wxFileExists(test));
			   dft += s;

			   wstring cfname = dft.ToStdWstring();
			   ofstream ofs(ws2s(cfname), ios::binary);
			   if (!ofs) return false;
			   curl_easy_setopt(s_curl_, CURLOPT_WRITEDATA, &ofs);
			   ret = curl_easy_perform(s_curl_);
			   bool succeeded = false;
			   if (ret == CURLE_OK)
				   succeeded = true;

			   ofs.close();

			   if (succeeded)
			   {
				   finfo->cached = true;
				   finfo->cache_filename = cfname;
				   cache_table_[finfo->filename] = cfname;
			   }
			   else
			   {
				   if(wxFileExists(cfname)) wxRemoveFile(cfname);
				   return false;
			   }
		   }
	   }

	   ifstream ifs;
	   wstring fn = finfo->cached ? finfo->cache_filename : finfo->filename;
	   ifs.open(ws2s(fn), ios::binary);
	   if (!ifs) 
		   return false;
       
       if (!finfo->isn5)
       {
           size_t zsize = finfo->datasize;
           if (zsize <= 0) zsize = (size_t)ifs.seekg(0, std::ios::end).tellg();
           char *zdata = new char[zsize];
           ifs.seekg(finfo->offset, ios_base::beg);
           ifs.read((char*)zdata, zsize);
           if (ifs) ifs.close();
           data = zdata;
           readsize = zsize;
       }
       else
       {
           size_t zsize = finfo->datasize;
           if (zsize <= 0) zsize = (size_t)ifs.seekg(0, std::ios::end).tellg();
           unsigned char *zdata = new unsigned char[zsize];
           char *blockdata = new char[zsize-16];
           ifs.seekg(finfo->offset, ios_base::beg);
           ifs.read((char*)zdata, zsize);
           if (ifs) ifs.close();
           
           if (zsize >= 16)
           {
               int mode = ((int)zdata[0] << 8) + (int)zdata[1];
               int dnum = ((int)zdata[2] << 8) + (int)zdata[3];
               if (dnum >= 1)
                   finfo->blosc_blocksize_x = ((int)zdata[4] << 24) + ((int)zdata[5] << 16) + ((int)zdata[6] << 8) + (int)zdata[7];
               if (dnum >= 2)
                   finfo->blosc_blocksize_y = ((int)zdata[8] << 24) + ((int)zdata[9] << 16) + ((int)zdata[10] << 8) + (int)zdata[11];
               if (dnum >= 3)
                   finfo->blosc_blocksize_z = ((int)zdata[12] << 24) + ((int)zdata[13] << 16) + ((int)zdata[14] << 8) + (int)zdata[15];
               if (mode == 1 && zsize >= 4 + dnum * 4 + 4)
               {
                   int idx = 4 + dnum * 4;
                   int numelems = ((int)zdata[idx] << 24) + ((int)zdata[idx+1] << 16) + ((int)zdata[idx+2] << 8) + (int)zdata[idx+3];
               }
               //cout << finfo->blosc_blocksize_x << " " << finfo->blosc_blocksize_y << " " << finfo->blosc_blocksize_z << endl;
           }
           
           memcpy(blockdata, zdata + 16, zsize-16);
           data = blockdata;
           readsize = zsize - 16;
       }

/*
       if (finfo->type != BRICK_FILE_TYPE_H265)
	   {
		   size_t zsize = finfo->datasize;
		   if (zsize <= 0) zsize = (size_t)ifs.seekg(0, std::ios::end).tellg();
		   char *zdata = new char[zsize];
		   ifs.seekg(finfo->offset, ios_base::beg);
		   ifs.read((char*)zdata, zsize);
		   if (ifs) ifs.close();
		   data = zdata;
		   readsize = zsize;
       }
	   else
	   {
		   size_t h265size = finfo->datasize;
		   size_t fsize = (size_t)ifs.seekg(0, std::ios::end).tellg();
		   if (h265size <= 0) h265size = fsize;
		   char *fdata = new char[fsize];
		   char *h265data = new char[h265size];
		   ifs.seekg(0, ios_base::beg);
		   ifs.read((char*)fdata, fsize);
		   if (ifs) ifs.close();
		   memcpy(h265data, fdata+finfo->offset, h265size);
		   data = h265data;
		   readsize = h265size;
		   memcache_table_[finfo->filename] = new MemCache(fdata, fsize);
		   memcache_order.push_back(finfo->filename);
		   memcache_size += fsize;

		   while (memcache_size > memcache_limit)
		   {
			   wstring name = *memcache_order.begin();
			   auto itr = memcache_table_.find(name);
			   if (itr != memcache_table_.end())
			   {
				   memcache_size -= itr->second->datasize;
				   delete itr->second;
				   memcache_table_.erase(itr);
			   }
			   memcache_order.pop_front();
		   }
	   }
*/
	   return true;
   }

   bool TextureBrick::decompress_brick(char *out, char* in, size_t out_size, size_t in_size, int type, int w, int h, int d, int nb, int n5_w, int n5_h, int n5_d, int endianness)
   {
       if      (type == BRICK_FILE_TYPE_N5RAW) return raw_decompressor(out, in, out_size, in_size, true, nb, w, h, d, n5_w, n5_h, n5_d, endianness);
	   else if (type == BRICK_FILE_TYPE_JPEG) return jpeg_decompressor(out, in, out_size, in_size);
	   else if (type == BRICK_FILE_TYPE_ZLIB) return zlib_decompressor(out, in, out_size, in_size);
	   else if (type == BRICK_FILE_TYPE_N5GZIP) return zlib_decompressor(out, in, out_size, in_size, true, nb, w, h, d, n5_w, n5_h, n5_d, endianness);
	   else if (type == BRICK_FILE_TYPE_H265) return h265_decompressor(out, in, out_size, in_size, w, h);
	   else if (type == BRICK_FILE_TYPE_LZ4) return lz4_decompressor(out, in, out_size, in_size);
       else if (type == BRICK_FILE_TYPE_N5LZ4) return lz4_decompressor(out, in, out_size, in_size, true, nb, w, h, d, n5_w, n5_h, n5_d, endianness);
       else if (type == BRICK_FILE_TYPE_N5ZSTD) return zstd_decompressor(out, in, out_size, in_size, true, nb, w, h, d, n5_w, n5_h, n5_d, endianness);
       else if (type == BRICK_FILE_TYPE_N5BLOSC) return blosc_decompressor(out, in, out_size, in_size, true, w, h, d, nb, n5_w, n5_h, n5_d, endianness);

	   return false;
   }


   // Function to switch endianness for 16-bit integers
   inline uint16_t switchEndianness16(uint16_t value) {
       return (value << 8) | (value >> 8);
   }

   // Function to switch endianness for 32-bit integers
   inline uint32_t switchEndianness32(uint32_t value) {
       return (value << 24) |
           ((value << 8) & 0x00FF0000) |
           ((value >> 8) & 0x0000FF00) |
           (value >> 24);
   }

   // Function to switch endianness for 64-bit integers
   inline uint64_t switchEndianness64(uint64_t value) {
       return (value << 56) |
           ((value << 40) & 0x00FF000000000000) |
           ((value << 24) & 0x0000FF0000000000) |
           ((value << 8) & 0x000000FF00000000) |
           ((value >> 8) & 0x00000000FF000000) |
           ((value >> 24) & 0x0000000000FF0000) |
           ((value >> 40) & 0x000000000000FF00) |
           (value >> 56);
   }

   // Template function to switch endianness of each element in a binary array
   template <typename T>
   void switchEndianness(T* data, size_t elementCount) {
       for (size_t i = 0; i < elementCount; ++i) {
           if constexpr (sizeof(T) == 2) {
               data[i] = switchEndianness16(data[i]);
           }
           else if constexpr (sizeof(T) == 4) {
               data[i] = switchEndianness32(data[i]);
           }
           else if constexpr (sizeof(T) == 8) {
               data[i] = switchEndianness64(data[i]);
           }
       }
   }
    
    bool TextureBrick::raw_decompressor(char* out, char* in, size_t out_size, size_t in_size, bool isn5, int nb, int w, int h, int d, int n5_w, int n5_h, int n5_d, int endianness)
    {
        if (!isn5)
            memcpy(out, in, in_size);
        else
        {
            size_t n5out_size = nb * n5_w * n5_h * n5_d;
            if (n5out_size <= 0 || n5out_size != in_size)
                return false;
            
            if (n5out_size != out_size)
            {
                char* src = in;
                char* dst = out;
                int src_y_pitch = n5_w * nb;
                int dst_y_pitch = w * nb;
                int src_z_pitch = n5_w * n5_h * nb;
                int dst_z_pitch = w * h * nb;
                for (int zz = 0; zz < d; zz++)
                {
                    for (int yy = 0; yy < h; yy++)
                        memcpy(dst + dst_y_pitch * yy, src + src_y_pitch * yy, dst_y_pitch);
                    src += src_z_pitch;
                    dst += dst_z_pitch;
                }
            }
            else
            {
                memcpy(out, in, in_size);
            }
            
            if (nb >= 2)
            {
                char e = check_machine_endian();
                if ((e == 'L' && endianness == BRICK_ENDIAN_BIG) || (e == 'B' && endianness == BRICK_ENDIAN_LITTLE))
                {
                    if (nb == 2)
                        switchEndianness((unsigned short*)out, out_size);
                    else if (nb == 4)
                        switchEndianness((unsigned int*)out, out_size);
                    else if (nb == 8)
                        switchEndianness((unsigned long*)out, out_size);
                    //for (int i = 0; i < out_size - 1; i += 2)
                    //    swap(out[i], out[i + 1]);
                }
            }
        }
        
        return true;
    }

   struct my_error_mgr {
	   struct jpeg_error_mgr pub;	/* "public" fields */

	   jmp_buf setjmp_buffer;	/* for return to caller */
   };

   typedef struct my_error_mgr * my_error_ptr;

   METHODDEF(void)
	   my_error_exit (j_common_ptr cinfo)
   {
	   /* cinfo->err really points to a my_error_mgr struct, so coerce pointer */
	   my_error_ptr myerr = (my_error_ptr) cinfo->err;

	   /* Always display the message. */
	   /* We could postpone this until after returning, if we chose. */
	   (*cinfo->err->output_message) (cinfo);

	   /* Return control to the setjmp point */
	   longjmp(myerr->setjmp_buffer, 1);
   }

   
   bool TextureBrick::jpeg_decompressor(char *out, char* in, size_t out_size, size_t in_size)
   {
	   jpeg_decompress_struct cinfo;
	   struct my_error_mgr jerr;
	   
	   cinfo.err = jpeg_std_error(&jerr.pub);
	   jerr.pub.error_exit = my_error_exit;
	   /* Establish the setjmp return context for my_error_exit to use. */
	   if (setjmp(jerr.setjmp_buffer)) {
		   /* If we get here, the JPEG code has signaled an error.
		   * We need to clean up the JPEG object, close the input file, and return.
		   */
		   jpeg_destroy_decompress(&cinfo);
		   return false;
	   }
	   jpeg_create_decompress(&cinfo);
	   jpeg_mem_src(&cinfo, (unsigned char *)in, in_size);
	   jpeg_read_header(&cinfo, TRUE);
	   jpeg_start_decompress(&cinfo);
	   
	   if(cinfo.jpeg_color_space != JCS_GRAYSCALE || cinfo.output_components != 1) longjmp(jerr.setjmp_buffer, 1);
	   
	   size_t jpgsize = cinfo.output_width * cinfo.output_height;
	   if (jpgsize != out_size) longjmp(jerr.setjmp_buffer, 1);
	   unsigned char *ptr[1];
	   ptr[0] = (unsigned char *)out;
	   while (cinfo.output_scanline < cinfo.output_height) {
		   ptr[0] = (unsigned char *)out + cinfo.output_scanline * cinfo.output_width;
		   jpeg_read_scanlines(&cinfo, ptr, 1);
	   }

	   jpeg_destroy_decompress(&cinfo);
	   
	   return true;
   }

   bool TextureBrick::zlib_decompressor(char *out, char* in, size_t out_size, size_t in_size, bool isn5, int nb, int w, int h, int d, int n5_w, int n5_h, int n5_d, int endianness)
   {
	   try
	   {
		   z_stream zInfo = {0};
		   zInfo.avail_in = in_size;
		   zInfo.total_in = 0;
		   zInfo.next_in = (Bytef*)in;

		   if (isn5)
		   {
               zInfo.avail_out = nb * n5_w * n5_h * n5_d;
               if (zInfo.avail_out <= 0)
               {
                   zInfo.avail_out = nb * w * h * d;
                   if (zInfo.avail_out <= 0)
                       return false;
               }
               
               bool use_buf = (zInfo.avail_out != out_size);
               char* buf = nullptr;
               if (use_buf)
                   buf = new char[zInfo.avail_out];
               
               zInfo.next_out = use_buf ? (Bytef*)buf : (Bytef*)out;
			   zInfo.total_out = 0;

			   int nErr, nOut = -1;
			   nErr = inflateInit2(&zInfo, MAX_WBITS | 32);
			   if (nErr == Z_OK) {
				   nErr = inflate(&zInfo, Z_FINISH);
				   if (nErr == Z_STREAM_END) {
					   nOut = zInfo.total_out;
				   }
			   }
			   inflateEnd(&zInfo);
			   if (nErr != Z_STREAM_END)
				   return false;
               
               if (use_buf)
               {
                   char* src = buf;
                   char* dst = out;
                   int src_y_pitch = n5_w * nb;
                   int dst_y_pitch = w * nb;
                   int src_z_pitch = n5_w * n5_h * nb;
                   int dst_z_pitch = w * h * nb;
                   for (int zz = 0; zz < d; zz++)
                   {
                       for (int yy = 0; yy < h; yy++)
                           memcpy(dst + dst_y_pitch * yy, src + src_y_pitch * yy, dst_y_pitch);
                       src += src_z_pitch;
                       dst += dst_z_pitch;
                   }
                   delete[] buf;
               }
               
               if (nb >= 2)
               {
                   char e = check_machine_endian();
                   if ((e == 'L' && endianness == BRICK_ENDIAN_BIG) || (e == 'B' && endianness == BRICK_ENDIAN_LITTLE))
                   {
                       if (nb == 2)
                           switchEndianness((unsigned short*)out, out_size);
                       else if (nb == 4)
                           switchEndianness((unsigned int*)out, out_size);
                       else if (nb == 8)
                           switchEndianness((unsigned long*)out, out_size);
                       //for (int i = 0; i < out_size - 1; i += 2)
                       //    swap(out[i], out[i + 1]);
                   }
               }
		   }
		   else
		   {
			   zInfo.avail_out = out_size;
			   zInfo.total_out = 0;
			   zInfo.next_out = (Bytef*)out;

			   int nErr, nOut = -1;
			   nErr = inflateInit(&zInfo);
			   if (nErr == Z_OK) {
				   nErr = inflate(&zInfo, Z_FINISH);
				   if (nErr == Z_STREAM_END) {
					   nOut = zInfo.total_out;
				   }
			   }
			   inflateEnd(&zInfo);
			   if (nOut != out_size || nErr != Z_STREAM_END)
				   return false;
		   }
	   }
	   catch (std::exception &e)
	   {
		   cerr << typeid(e).name() << "\n" << e.what() << endl;
		   return false;
	   }

	   return true;
   }

   bool TextureBrick::h265_decompressor(char *out, char* in, size_t out_size, size_t in_size, int w, int h)
   {
	   FFMpegMemDecoder decoder((void *)in, in_size, 0, 0);
	   decoder.start();
	   int vw = decoder.getImageWidth();
	   int vh = decoder.getImageHeight();
	   if (vw - w >= 0) decoder.setImagePaddingR(vw - w);
	   if (vh - h >= 0) decoder.setImagePaddingB(vh - h);
	   decoder.grab(out);
	   decoder.close();
	   return true;
   }


   bool TextureBrick::lz4_decompressor(char* out, char* in, size_t out_size, size_t in_size, bool isn5, int nb, int w, int h, int d, int n5_w, int n5_h, int n5_d, int endianness)
   {
       /*
       LZ4F_dctx* dctx = nullptr;
       {
           size_t const dctxStatus = LZ4F_createDecompressionContext(&dctx, LZ4F_VERSION);
           if (LZ4F_isError(dctxStatus)) {
               cerr << "LZ4F_dctx creation error: " << LZ4F_getErrorName(dctxStatus) << endl;
               return false;
           }
       }
       */
       if (!isn5)
       {
           /*
           size_t ret = LZ4F_decompress(dctx, out, &out_size, in, &in_size, NULL);
           if (LZ4F_isError(ret)) {
               cerr << "Decompression error: " << LZ4F_getErrorName(ret) << endl;
               return false;
           }
           */
           const int decompressed_size = LZ4_decompress_safe(in, out, in_size, out_size);
           if (decompressed_size != out_size || decompressed_size < 0)
               return false;
           
       }
       else
       {
           size_t n5out_size = nb * n5_w * n5_h * n5_d;
           if (n5out_size <= 0)
               return false;
           
           bool use_buf = (n5out_size != out_size);
           char* buf = nullptr;
           if (use_buf)
               buf = new char[n5out_size];
           
           char *lz4_out = use_buf ? buf : out;
           /*
           size_t ret = LZ4F_decompress(dctx, lz4_out, &n5out_size, in, &in_size, NULL);
           if (LZ4F_isError(ret)) {
               cerr << "Decompression error: " << LZ4F_getErrorName(ret) << endl;
               return false;
           }
           */
           const int decompressed_size = LZ4_decompress_safe(in, lz4_out, in_size, n5out_size);
           if (decompressed_size != n5out_size || decompressed_size < 0)
               return false;
           
           if (use_buf)
           {
               char* src = buf;
               char* dst = out;
               int src_y_pitch = n5_w * nb;
               int dst_y_pitch = w * nb;
               int src_z_pitch = n5_w * n5_h * nb;
               int dst_z_pitch = w * h * nb;
               for (int zz = 0; zz < d; zz++)
               {
                   for (int yy = 0; yy < h; yy++)
                       memcpy(dst + dst_y_pitch * yy, src + src_y_pitch * yy, dst_y_pitch);
                   src += src_z_pitch;
                   dst += dst_z_pitch;
               }
               delete[] buf;
           }
           
           if (nb >= 2)
           {
               char e = check_machine_endian();
               if ((e == 'L' && endianness == BRICK_ENDIAN_BIG) || (e == 'B' && endianness == BRICK_ENDIAN_LITTLE))
               {
                   if (nb == 2)
                       switchEndianness((unsigned short*)out, out_size);
                   else if (nb == 4)
                       switchEndianness((unsigned int*)out, out_size);
                   else if (nb == 8)
                       switchEndianness((unsigned long*)out, out_size);
                   //for (int i = 0; i < out_size - 1; i += 2)
                   //    swap(out[i], out[i + 1]);
               }
           }
       }
       
       //LZ4F_freeDecompressionContext(dctx);

	   return true;
   }
   
   bool TextureBrick::zstd_decompressor(char* out, char* in, size_t out_size, size_t in_size, bool isn5, int nb, int w, int h, int d, int n5_w, int n5_h, int n5_d, int endianness)
   {
       if (!isn5)
       {
           std::vector<char> decompressedData;
           try {
               // Create an input stream from the compressed data
               boost::iostreams::array_source compressedSource(in, in_size);
               boost::iostreams::filtering_istream in;
               in.push(boost::iostreams::zstd_decompressor());
               in.push(compressedSource);

               // Read directly from the decompression stream into the output buffer
               size_t bytesRead = 0;
               while (in && bytesRead < out_size) {
                   in.read(out + bytesRead, out_size - bytesRead);
                   bytesRead += in.gcount();  // Increment by the number of bytes read in the last read operation
               }

               // Check if the decompression was successful and fits within the output buffer
               if (in.bad() || bytesRead > out_size) {
                   std::cerr << "Decompression error or output buffer too small." << std::endl;
                   return false;
               }
           }
           catch (const std::exception & e) {
               std::cerr << "Decompression error: " << e.what() << std::endl;
               return false;
           }
       }
       else
       {
           size_t n5out_size = nb * n5_w * n5_h * n5_d;
           if (n5out_size <= 0)
               return false;

           bool use_buf = (n5out_size != out_size);
           char* buf = nullptr;
           if (use_buf)
               buf = new char[n5out_size];

           char* zstd_out = use_buf ? buf : out;

           try {
               // Create an input stream from the compressed data
               boost::iostreams::array_source compressedSource(in, in_size);
               boost::iostreams::filtering_istream in;
               in.push(boost::iostreams::zstd_decompressor());
               in.push(compressedSource);

               // Read directly from the decompression stream into the output buffer
               size_t bytesRead = 0;
               while (in && bytesRead < n5out_size) {
                   in.read(zstd_out + bytesRead, n5out_size - bytesRead);
                   bytesRead += in.gcount();  // Increment by the number of bytes read in the last read operation
               }

               // Check if the decompression was successful and fits within the output buffer
               if (in.bad() || bytesRead > n5out_size) {
                   std::cerr << "Decompression error or output buffer too small." << std::endl;
                   return false;
               }
           }
           catch (const std::exception & e) {
               std::cerr << "Decompression error: " << e.what() << std::endl;
               return false;
           }

           if (use_buf)
           {
               char* src = buf;
               char* dst = out;
               int src_y_pitch = n5_w * nb;
               int dst_y_pitch = w * nb;
               int src_z_pitch = n5_w * n5_h * nb;
               int dst_z_pitch = w * h * nb;
               for (int zz = 0; zz < d; zz++)
               {
                   for (int yy = 0; yy < h; yy++)
                       memcpy(dst + dst_y_pitch * yy, src + src_y_pitch * yy, dst_y_pitch);
                   src += src_z_pitch;
                   dst += dst_z_pitch;
               }
               delete[] buf;
           }

           if (nb >= 2)
           {
               char e = check_machine_endian();
               if ( (e == 'L' && endianness == BRICK_ENDIAN_BIG) || (e == 'B' && endianness == BRICK_ENDIAN_LITTLE))
               {
                   if (nb == 2)
                       switchEndianness((unsigned short*)out, out_size);
                   else if (nb == 4)
                       switchEndianness((unsigned int*)out, out_size);
                   else if (nb == 8)
                       switchEndianness((unsigned long*)out, out_size);
                   //for (int i = 0; i < out_size - 1; i += 2)
                   //    swap(out[i], out[i + 1]);
               }
           }
       }

       return true;
   }
    
    bool TextureBrick::blosc_decompressor(char* out, char* in, size_t out_size, size_t in_size, bool isn5, int w, int h, int d, int nb, int n5_w, int n5_h, int n5_d, int endianness)
    {
        blosc2_dparams params;
        params.nthreads = 1;
        params.schunk = NULL;
        
        auto ctx = blosc2_create_dctx(params);
        
        if (!ctx)
            return false;
        
        int decompressed_size = blosc2_decompress_ctx(ctx, in, in_size, out, out_size);
        if (decompressed_size < 0)
        {
            int32_t nbytes, cbytes, bs;
            blosc2_cbuffer_sizes(in, &nbytes, &cbytes, &bs);
            
            char* buf = new char[nbytes];
            int decompressed_size2 = blosc2_decompress_ctx(ctx, in, in_size, buf, nbytes);
            if (decompressed_size2 < 0 || n5_w < w || n5_h < h || n5_d < d)
                return false;
            
            char* src = buf;
            char* dst = out;
            int src_y_pitch = n5_w * nb;
            int dst_y_pitch = w * nb;
            int src_z_pitch = n5_w * n5_h * nb;
            int dst_z_pitch = w * h * nb;
            for (int zz = 0; zz < d; zz++)
            {
                for (int yy = 0; yy < h; yy++)
                    memcpy(dst + dst_y_pitch * yy, src + src_y_pitch * yy, dst_y_pitch);
                src += src_z_pitch;
                dst += dst_z_pitch;
            }
            delete[] buf;
        }
        
        if (isn5 && nb >= 2)
        {
            char e = check_machine_endian();
            if ((e == 'L' && endianness == BRICK_ENDIAN_BIG) || (e == 'B' && endianness == BRICK_ENDIAN_LITTLE))
            {
                if (nb == 2)
                    switchEndianness((unsigned short*)out, out_size);
                else if (nb == 4)
                    switchEndianness((unsigned int*)out, out_size);
                else if (nb == 8)
                    switchEndianness((unsigned long*)out, out_size);
                //for (int i = 0; i < out_size - 1; i += 2)
                //    swap(out[i], out[i + 1]);
            }
        }
        
        blosc2_free_ctx(ctx);
        
        return true;
    }
    
    char TextureBrick::check_machine_endian()
    {
        char e='N'; //for unknown endianness
        
        long int a=0x44332211;
        unsigned char * p = (unsigned char *)&a;
        if ((*p==0x11) && (*(p+1)==0x22) && (*(p+2)==0x33) && (*(p+3)==0x44))
            e = 'L';
        else if ((*p==0x44) && (*(p+1)==0x33) && (*(p+2)==0x22) && (*(p+3)==0x11))
            e = 'B';
        else if ((*p==0x22) && (*(p+1)==0x11) && (*(p+2)==0x44) && (*(p+3)==0x33))
            e = 'M';
        else
            e = 'N';
        
        return e;
    }

   void TextureBrick::delete_all_cache_files()
   {
	   for(auto itr = cache_table_.begin(); itr != cache_table_.end(); ++itr)
	   {
		   wxString tmp = itr->second;
		   if(wxFileExists(tmp)) wxRemoveFile(tmp);
	   }
	   if (!cache_table_.empty()) cache_table_.clear();
   }

   bool TextureBrick::raw_brick_reader(char* data, size_t size, const FileLocInfo* finfo)
   {
	   try
	   {
		   ifstream ifs;
		   ifs.open(ws2s(finfo->filename), ios::binary);
		   if (!ifs) return false;
		   if (finfo->datasize > 0 && size != finfo->datasize) return false;
		   size_t read_size = finfo->datasize > 0 ? finfo->datasize : size;
		   ifs.seekg(finfo->offset, ios_base::beg); 
		   ifs.read(data, read_size);
		   if (ifs) ifs.close();
/*
		   FILE* fp = fopen(ws2s(finfo->filename).c_str(), "rb");
		   if (!fp) return false;
		   if (finfo->datasize > 0 && size != finfo->datasize) return false;
		   size_t read_size = finfo->datasize > 0 ? finfo->datasize : size;
		   setvbuf(fp, NULL, _IONBF, 0);
		   fseek(fp, finfo->offset, SEEK_SET); 
		   fread(data, 0x1, read_size, fp);
		   if (fp) fclose(fp);
*//*		   
		   ofstream ofs1;
		   wstring str = *fname + wstring(L".txt");
		   ofs1.open(str);
		   for(int i=0; i < size/2; i++){
			   ofs1 << ((unsigned short *)data)[i] << "\n";
		   }
		   ofs1.close();
*/		   
	   }
	   catch (std::exception &e)
	   {
		   cerr << typeid(e).name() << "\n" << e.what() << endl;
		   return false;
	   }

	   return true;
   }

   bool TextureBrick::raw_brick_reader_url(char* data, size_t size, const FileLocInfo* finfo)
   {
	   CURLcode ret;
	   struct MemoryStruct chunk;
	   chunk.memory = (char *)malloc(1);  /* will be grown as needed by the realloc above */ 
	   chunk.size = 0;

	   if (s_curl_ == NULL) {
		   cerr << "curl_easy_init() failed" << endl;
		   return false;
	   }
	   curl_easy_reset(s_curl_);
	   curl_easy_setopt(s_curl_, CURLOPT_URL, wxString(finfo->filename).ToStdString().c_str());
       curl_easy_setopt(s_curl_, CURLOPT_TIMEOUT, 10L);
	   curl_easy_setopt(s_curl_, CURLOPT_USERAGENT, "libcurl-agent/1.0");
	   curl_easy_setopt(s_curl_, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
	   curl_easy_setopt(s_curl_, CURLOPT_WRITEDATA, &chunk);
	   //�s�A�ؖ������؂Ȃ�
	   curl_easy_setopt(s_curl_, CURLOPT_SSL_VERIFYPEER, 0);
	   ret = curl_easy_perform(s_curl_);
	   if (ret != CURLE_OK) {
		   cerr << "curl_easy_perform() failed." << curl_easy_strerror(ret) << endl;
		   return false;
	   }
	   if(chunk.size != size) return false;

	   memcpy(data, chunk.memory, size);

	   if(chunk.memory)
		   free(chunk.memory);

	   return true;
   }

   bool TextureBrick::jpeg_brick_reader(char* data, size_t size, const FileLocInfo* finfo)
   {
	   jpeg_decompress_struct cinfo;
	   struct my_error_mgr jerr;
	   
       FILE *fp = 0;
	   if (!WFOPEN(&fp, finfo->filename.c_str(), L"rb"))
           return false;

	   cinfo.err = jpeg_std_error(&jerr.pub);
	   jerr.pub.error_exit = my_error_exit;
	   /* Establish the setjmp return context for my_error_exit to use. */
	   if (setjmp(jerr.setjmp_buffer)) {
		   /* If we get here, the JPEG code has signaled an error.
		   * We need to clean up the JPEG object, close the input file, and return.
		   */
		   jpeg_destroy_decompress(&cinfo);
		   fclose(fp);
		   return false;
	   }
	   jpeg_create_decompress(&cinfo);

	   fseek(fp, finfo->offset, SEEK_SET);
	   jpeg_stdio_src(&cinfo, fp);
	   jpeg_read_header(&cinfo, TRUE);
	   jpeg_start_decompress(&cinfo);
	   
	   if(cinfo.jpeg_color_space != JCS_GRAYSCALE || cinfo.output_components != 1) longjmp(jerr.setjmp_buffer, 1);
	   
	   size_t jpgsize = cinfo.output_width * cinfo.output_height;
	   if (jpgsize != size) longjmp(jerr.setjmp_buffer, 1);
	   unsigned char *ptr[1];
	   ptr[0] = (unsigned char *)data;
	   while (cinfo.output_scanline < cinfo.output_height) {
		   ptr[0] = (unsigned char *)data + cinfo.output_scanline * cinfo.output_width;
		   jpeg_read_scanlines(&cinfo, ptr, 1);
	   }

	   jpeg_destroy_decompress(&cinfo);
	   fclose(fp);
	   
	   return true;
   }

   bool TextureBrick::jpeg_brick_reader_url(char* data, size_t size, const FileLocInfo* finfo)
   {
	   jpeg_decompress_struct cinfo;
	   struct my_error_mgr jerr;
	   
	   CURLcode ret;
	   struct MemoryStruct chunk;
	   chunk.memory = (char *)malloc(1);
	   chunk.size = 0;

	   if (s_curl_ == NULL) {
		   cerr << "curl_easy_init() failed" << endl;
		   return false;
	   }
	   curl_easy_reset(s_curl_);
	   curl_easy_setopt(s_curl_, CURLOPT_URL, wxString(finfo->filename).ToStdString().c_str());
       curl_easy_setopt(s_curl_, CURLOPT_TIMEOUT, 10L);
	   curl_easy_setopt(s_curl_, CURLOPT_USERAGENT, "libcurl-agent/1.0");
	   curl_easy_setopt(s_curl_, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
	   curl_easy_setopt(s_curl_, CURLOPT_WRITEDATA, &chunk);
	   //�s�A�ؖ������؂Ȃ�
	   curl_easy_setopt(s_curl_, CURLOPT_SSL_VERIFYPEER, 0);
	   ret = curl_easy_perform(s_curl_);
	   if (ret != CURLE_OK) {
		   cerr << "curl_easy_perform() failed." << curl_easy_strerror(ret) << endl;
		   return false;
	   }
	   
	   cinfo.err = jpeg_std_error(&jerr.pub);
	   jerr.pub.error_exit = my_error_exit;
	   /* Establish the setjmp return context for my_error_exit to use. */
	   if (setjmp(jerr.setjmp_buffer)) {
		   /* If we get here, the JPEG code has signaled an error.
		   * We need to clean up the JPEG object, close the input file, and return.
		   */
		   jpeg_destroy_decompress(&cinfo);
		   return false;
	   }
	   jpeg_create_decompress(&cinfo);
	   jpeg_mem_src(&cinfo, (unsigned char *)chunk.memory, chunk.size);
	   //jpeg_mem_src(&cinfo, (unsigned char *)mem_buf.GetData(), mem_buf.GetDataLen());
	   jpeg_read_header(&cinfo, TRUE);
	   jpeg_start_decompress(&cinfo);
	   
	   if(cinfo.jpeg_color_space != JCS_GRAYSCALE || cinfo.output_components != 1) longjmp(jerr.setjmp_buffer, 1);
	   
	   size_t jpgsize = cinfo.output_width * cinfo.output_height;
	   if (jpgsize != size) longjmp(jerr.setjmp_buffer, 1);
	   unsigned char *ptr[1];
	   ptr[0] = (unsigned char *)data;
	   while (cinfo.output_scanline < cinfo.output_height) {
		   ptr[0] = (unsigned char *)data + cinfo.output_scanline * cinfo.output_width;
		   jpeg_read_scanlines(&cinfo, ptr, 1);
	   }

	   jpeg_destroy_decompress(&cinfo);
	   
	   if(chunk.memory)
		   free(chunk.memory);
	   	   
	   return true;
   }

   bool TextureBrick::zlib_brick_reader(char* data, size_t size, const FileLocInfo* finfo)
   {
	   try
	   {
		   ifstream ifs;
		   ifs.open(ws2s(finfo->filename), ios::binary);
		   if (!ifs) return false;
		   
		   size_t zsize = finfo->datasize;
		   if (zsize <= 0) zsize = (size_t)ifs.seekg(0, std::ios::end).tellg();
		   
		   unsigned char *zdata = new unsigned char[zsize];
		   ifs.seekg(finfo->offset, ios_base::beg);
		   ifs.read((char*)zdata, zsize);
		   if (ifs) ifs.close();

		   z_stream zInfo = {0};
		   zInfo.total_in  = zInfo.avail_in  = zsize;
		   zInfo.total_out = zInfo.avail_out = size;
		   zInfo.next_in  = zdata;
		   zInfo.next_out = (Bytef *)data;

		   int nErr, nOut = -1;
		   nErr = inflateInit( &zInfo );
		   if ( nErr == Z_OK ) {
			   nErr = inflate( &zInfo, Z_FINISH );
			   if ( nErr == Z_STREAM_END ) {
				   nOut = zInfo.total_out;
			   }
		   }
		   inflateEnd( &zInfo );
		   delete [] zdata;

		   if (nOut != size || nErr != Z_STREAM_END)
			   return false;
	   }
	   catch (std::exception &e)
	   {
		   cerr << typeid(e).name() << "\n" << e.what() << endl;
		   return false;
	   }

	   return true;
   }

   bool TextureBrick::zlib_brick_reader_url(char* data, size_t size, const FileLocInfo* finfo)
   {
	   return true;
   }
#endif

} // end namespace FLIVR
