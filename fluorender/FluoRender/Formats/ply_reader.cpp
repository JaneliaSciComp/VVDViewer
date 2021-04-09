#define _USE_MATH_DEFINES
#include "ply_reader.h"
#include "happly.h"
#include "../compatibility.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <locale>
#include <algorithm>
#include <map>

GLMmodel *PLYReader::GetSolidModel()
{
    std::vector<std::array<double, 3>> vPos;
    std::vector<std::vector<size_t>> fInd;
    
    try {
        happly::PLYData plyIn(ws2s(m_path_name).c_str());
        plyIn.validate();
    
        vPos = plyIn.getVertexPositions();
        fInd = plyIn.getFaceIndices<size_t>();
    }
    catch (const std::exception & e)
    {
        std::cerr << "tinyply exception: " << e.what() << std::endl;
        return NULL;
    }
    
    vector<unsigned int> model_tris;
	GLMmodel *model = (GLMmodel*)malloc(sizeof(GLMmodel));
	float *verts = (float*)malloc( (sizeof(float)*vPos.size() + 1) * 3 );

    for (int i = 0; i < vPos.size(); i++)
    {
        verts[3*i]   = vPos[i][0];
        verts[3*i+1] = vPos[i][1];
        verts[3*i+2] = vPos[i][2];
    }
	
    size_t count = 0;
	for (int i = 0; i < fInd.size(); i++)
	{
        for (int j = 2; j < fInd[i].size(); j++)
        {
            model_tris.push_back(fInd[i][0]);
            model_tris.push_back(fInd[i][j-1]);
            model_tris.push_back(fInd[i][j]);
            count++;
        }
	}
    
    GLMtriangle *tris = (GLMtriangle*)malloc(sizeof(GLMtriangle)*model_tris.size()/3);
    unsigned int *gtris = (unsigned int*)malloc(sizeof(unsigned int)*model_tris.size()/3);
    int numtris = model_tris.size()/3;
    vector<unsigned int>::iterator t_ite = model_tris.begin();
    for (int i = 0; i < numtris; i++)
    {
        tris[i].vindices[0] = *t_ite;
        tris[i].nindices[0] = *t_ite++;
        tris[i].vindices[1] = *t_ite;
        tris[i].nindices[1] = *t_ite++;
        tris[i].vindices[2] = *t_ite;
        tris[i].nindices[2] = *t_ite++;
        gtris[i] = i;
    }

	model->pathname    = STRDUP(ws2s(m_path_name).c_str());
	model->mtllibname    = NULL;
	model->numvertices   = vPos.size();
	model->vertices    = verts;
    model->numnormals    = 0;
	model->normals     = NULL;
	model->numtexcoords  = 0;
	model->texcoords       = NULL;
	model->numfacetnorms = 0;
	model->facetnorms    = NULL;
	model->numtriangles  = numtris;
	model->triangles       = tris;
	model->numlines = 0;
	model->lines = NULL;
	model->nummaterials  = 0;
	model->materials       = NULL;
	model->numgroups       = 0;
	model->groups      = NULL;
	model->position[0]   = 0.0;
	model->position[1]   = 0.0;
	model->position[2]   = 0.0;
	model->hastexture = false;
	
	GLMgroup* group;
	group = (GLMgroup*)malloc(sizeof(GLMgroup));
	group->name = STRDUP("default");
	group->material = 0;
	group->numtriangles = numtris;
	group->triangles = gtris;
	group->next = model->groups;
	model->groups = group;
	model->numgroups++;
    
    /*
    if (!model->normals && model->numtriangles)
    {
        if (!model->facetnorms)
            glmFacetNormals(model);
        glmVertexNormals(model, 89.0);
    }
    */

	return model;
}

PLYReader::PLYReader()
{
}

PLYReader::~PLYReader()
{
}

void PLYReader::SetFile(string &file)
{
	if (!file.empty())
	{
		if (!m_path_name.empty())
			m_path_name.clear();
		m_path_name.assign(file.length(), L' ');
		copy(file.begin(), file.end(), m_path_name.begin());
	}
}

void PLYReader::SetFile(wstring &file)
{
	m_path_name = file;
}

bool PLYReader::DeepCopy(PLYReader *in, PLYReader *out)
{
	if (in == nullptr || out == nullptr)
		return false;

	out->m_data_name = in->m_data_name;
	out->m_path_name = in->m_path_name;

	return true;
}
