/*
For more information, please see: http://software.sci.utah.edu
The MIT License
Copyright (c) 2014 Scientific Computing and Imaging Institute,
University of Utah.
Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
*/
#ifndef _INTERPOLATOR_H_
#define _INTERPOLATOR_H_

#include <vector>
#include <string>
#include "FlKey.h"
#include "FlKeyDouble.h"
#include "FlKeyQuaternion.h"
#include "FlKeyBoolean.h"
#include "FlKeyInt.h"
#include "../FLIVR/Quaternion.h"

#include "DLLExport.h"

using namespace std;
using namespace FLIVR;

typedef struct
{
	int id;		//identifier
	double t;	//time: 0-linear; 1-spline
	int type;	//interpolation method
	vector<FlKey*> keys; //keys
	string desc;//descriptions
} FlKeyGroup;

class EXPORT_API Interpolator
{
public:
	Interpolator();
	~Interpolator();

	//create
	//return group id
	int Begin(double t);
	//return successfulness
	int AddKey(FlKey *key);
	//return completeness
	int End();

	//get info
	int GetKeyNum();
	double GetFirstT();
	double GetLastT();
	FlKeyGroup* GetKeyGroup(int index);
	int GetKeyIndex(int id);
	int GetKeyIndexFromTime(double t);
	int GetKeyID(int index);
	double GetKeyTime(int index);
	double GetKeyDuration(int index);
	int GetKeyType(int index);
	string GetKeyDesc(int index);
	int GetLastIndex()
	{ return (int)m_key_list.size() - 1;}
	vector<FlKeyGroup*>* GetKeyList()
	{ return &m_key_list; }

	//modify
	void Clear();
	void RemoveKey(int id);
	void ChangeTime(int index, double time);
	void ChangeDuration(int index, double duration);
	void MoveKeyBefore(int from_idx, int to_idx);
	void MoveKeyAfter(int from_idx, int to_idx);

	//get values
	bool GetDouble(FLKeyCode keycode, double t, double &dval);
	bool GetQuaternion(FLKeyCode keycode, double t, Quaternion &qval);
	bool GetBoolean(FLKeyCode keycode, double t, bool &bval);
	bool GetInt(FLKeyCode keycode, double t, int &ival);

	static int m_id;

private:
	//adding: 0-disabled; 1-enabled
	int m_adding;
	vector<FlKeyGroup*> m_key_list;

private:
	FlKey* SearchKey(FLKeyCode keycode, FlKeyGroup* g);
	bool StepDouble(FLKeyCode keycode, FlKeyGroup* g, double &dval);
	bool LinearDouble(FLKeyCode keycode, FlKeyGroup* g1, FlKeyGroup* g2, double t, double &dval);
	bool StepQuaternion(FLKeyCode keycode, FlKeyGroup* g, Quaternion &qval);
	bool LinearQuaternion(FLKeyCode keycode, FlKeyGroup* g1, FlKeyGroup* g2, double t, Quaternion &qval);
	double Smooth(double ft, bool s1, bool s2);

};

#endif//_INTERPOLATOR_H_