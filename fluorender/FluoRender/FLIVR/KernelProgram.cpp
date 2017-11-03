#include "KernelProgram.h"
#ifdef _WIN32
#include <Windows.h>
#endif

namespace FLIVR
{
	bool KernelProgram::init_ = false;
	cl_device_id KernelProgram::device_ = 0;
	cl_context KernelProgram::context_ = 0;
	int KernelProgram::device_id_ = 0;
	std::string KernelProgram::device_name_;
#ifdef _DARWIN
    CGLContextObj KernelProgram::gl_context_ = 0;
#endif
	KernelProgram::KernelProgram(const std::string& source) :
	source_(source), program_(0), queue_(0)
	{
	}

	KernelProgram::~KernelProgram()
	{
		destroy();
	}

	void KernelProgram::init_kernels_supported()
	{
		if (init_)
			return;

		cl_int err;
		cl_uint ret_num_platforms;
		cl_platform_id platforms[4];
        int best_pid = -1;
        int best_devid = -1;
        cl_device_id best_dev = 0;
        long best_spec = 0;
		
		err = clGetPlatformIDs(4, platforms, &ret_num_platforms);
		if (err != CL_SUCCESS)
			return;

		for (int pidx = 0; pidx < ret_num_platforms; ++pidx)
		{
			cl_device_id devices[4];
			cl_uint dev_num = 0;
			err = clGetDeviceIDs(platforms[pidx], CL_DEVICE_TYPE_GPU, 4, devices, &dev_num);
			if (err != CL_SUCCESS)
				continue;
			if (device_id_ >=0 && device_id_ < dev_num)
				device_ = devices[device_id_];
			else
				device_ = devices[0];
			
            
            for (int didx = 0; didx < dev_num; didx++)
            {
                char buffer[10240];
                clGetDeviceInfo(devices[didx], CL_DEVICE_NAME, sizeof(buffer), buffer, NULL);
                device_name_ = std::string(buffer);
            
                cl_uint freq = 0;
                clGetDeviceInfo(devices[didx], CL_DEVICE_MAX_CLOCK_FREQUENCY, sizeof(freq), &freq, NULL);
                cl_uint units = 0;
                clGetDeviceInfo(devices[didx], CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(units), &units, NULL);
                if (best_spec < freq * units)
                {
                    best_pid = pidx;
                    best_devid = didx;
                    best_dev = devices[didx];
                    best_spec = freq * units;
                }
            }
        }
        
        if (best_pid < 0 || best_devid < 0)
            return;
        device_id_ = best_devid;
        device_ = best_dev;
        
#ifdef _DARWIN
        gl_context_ =CGLGetCurrentContext();
#endif
        cl_context_properties properties[] =
        {
#ifdef _WIN32
            CL_GL_CONTEXT_KHR, (cl_context_properties)wglGetCurrentContext(),
            CL_WGL_HDC_KHR, (cl_context_properties)wglGetCurrentDC(),
            CL_CONTEXT_PLATFORM, (cl_context_properties)platforms[best_pid],
#endif
#ifdef _DARWIN
            CL_CONTEXT_PROPERTY_USE_CGL_SHAREGROUP_APPLE,
            (cl_context_properties) CGLGetShareGroup( gl_context_),
#endif
            0
        };
#ifdef _WIN32
        context_ = clCreateContext(properties, 1, &device_, NULL, NULL, &err);
#else
        context_ = clCreateContext(properties, 0, NULL, NULL, NULL, &err);
        size_t deviceBufferSize = -1;
        clGetContextInfo(context_, CL_CONTEXT_DEVICES, 0, NULL, &deviceBufferSize);
        
        if (deviceBufferSize == 0)
        {
            fprintf(stderr, "No OpenCL devices available\n");
            return;
        }
        
        cl_device_id *devices = (cl_device_id *)malloc(deviceBufferSize);
        clGetContextInfo(context_, CL_CONTEXT_DEVICES, deviceBufferSize, devices, NULL);
        device_ = devices[0];
        int dev_num = deviceBufferSize / sizeof(cl_device_id);
        for (int didx = 0; didx < dev_num; didx++)
        {
            if (devices[didx] == best_dev)
                device_ = best_dev;
        }
#endif
        if (err != CL_SUCCESS)
            return;
			
        init_ = true;
	}

	bool KernelProgram::init()
	{
		return init_;
	}

	void KernelProgram::clear()
	{
		clReleaseContext(context_);
		init_ = false;
	}

	void KernelProgram::set_device_id(int id)
	{
		device_id_ = id;
	}

	int KernelProgram::get_device_id()
	{
		return device_id_;
	}

	std::string& KernelProgram::get_device_name()
	{
		return device_name_;
	}

	bool KernelProgram::create(std::string &name)
	{
		cl_int err;
		if (!program_)
		{
			const char *c_source[1];
			c_source[0] = source_.c_str();
			size_t program_size = source_.size();
			program_ = clCreateProgramWithSource(context_, 1,
				c_source, &program_size, &err);
			if (err != CL_SUCCESS)
			{
				return false;
			}

			err = clBuildProgram(program_, 0, NULL, NULL, NULL, NULL);
			info_.clear();
			if (err != CL_SUCCESS)
			{
				char *program_log;
				size_t log_size = 0;
				clGetProgramBuildInfo(program_, device_, CL_PROGRAM_BUILD_LOG,
					0, NULL, &log_size);
				program_log = new char[log_size+1];
				program_log[log_size] = '\0';
				clGetProgramBuildInfo(program_, device_, CL_PROGRAM_BUILD_LOG,
					log_size+1, program_log, NULL);
				info_ = program_log;
				delete []program_log;
				return false;
			}
		}

		cl_kernel kl = clCreateKernel(program_, name.c_str(), &err);
		if (err != CL_SUCCESS)
		{
			return false;
		}
        if (kernel_.find(name) != kernel_.end())
            clReleaseKernel(kernel_[name]);
		kernel_[name] = kl;

		if (!queue_)
		{
			queue_ = clCreateCommandQueue(context_, device_, 0, &err);
			if (err != CL_SUCCESS)
			{
				return false;
			}
		}

		return true;
	}

	bool KernelProgram::valid(std::string name)
	{
		if (kernel_.empty())
			return false;

		if (name.empty())
			return init_ && program_ && kernel_.begin()->second && queue_;
		else
		{
			if (kernel_.find(name) == kernel_.end())
				return false;
			else
				return init_ && program_ && kernel_[name] && queue_;
		}
	}

	void KernelProgram::destroy()
	{
		clFinish(queue_);

		auto it = kernel_.begin();
		while (it != kernel_.end())
		{
			clReleaseKernel((*it).second);
			++it;
		}

		for (unsigned int i=0; i<arg_list_.size(); ++i)
			clReleaseMemObject(arg_list_[i].buffer);
		clReleaseCommandQueue(queue_);
		clReleaseProgram(program_);
	}

	void KernelProgram::execute(cl_uint dim, size_t *global_size, size_t *local_size, std::string name)
	{
		if (!valid(name))
			return;

		cl_kernel kl;

		if (name.empty())
			kl = kernel_.begin()->second;
		else
			kl = kernel_[name];

		cl_int err;
		glFinish();
		unsigned int i;
		for (i=0; i<arg_list_.size(); ++i)
		{
			if (arg_list_[i].size == 0)
			{
				err = clEnqueueAcquireGLObjects(queue_, 1, &(arg_list_[i].buffer), 0, NULL, NULL);
				if (err != CL_SUCCESS)
					return;
			}
		}
		err = clEnqueueNDRangeKernel(queue_, kl, dim, NULL, global_size,
			local_size, 0, NULL, NULL);
		if (err != CL_SUCCESS)
			return;
		for (i=0; i<arg_list_.size(); ++i)
		{
			if (arg_list_[i].size == 0)
			{
				err = clEnqueueReleaseGLObjects(queue_, 1, &(arg_list_[i].buffer), 0, NULL, NULL);
				if (err != CL_SUCCESS)
					return;
			}
		}
		clFinish(queue_);
	}

	bool KernelProgram::matchArg(Argument* arg, unsigned int& arg_index)
	{
		for (unsigned int i=0; i<arg_list_.size(); ++i)
		{
			if (arg_list_[i].size == arg->size &&
				arg_list_[i].texture == arg->texture &&
				arg_list_[i].buf_src == arg->buf_src)
			{
				arg_index = i;
				return true;
			}
		}
		return false;
	}

	bool KernelProgram::delBuf(void *data)
	{
		for (auto ite = arg_list_.begin(); ite != arg_list_.end(); ++ite)
		{
			if (ite->buf_src == data && ite->buffer)
			{
				clReleaseMemObject(ite->buffer);
				arg_list_.erase(ite);
				return true;
			}
		}
		return false;
	}

	bool KernelProgram::delTex(GLuint texture)
	{
		for (auto ite = arg_list_.begin(); ite != arg_list_.end(); ++ite)
		{
			if (ite->texture == texture && ite->buffer)
			{
				clReleaseMemObject(ite->buffer);
				arg_list_.erase(ite);
				return true;
			}
		}
		return false;
	}

	void KernelProgram::setKernelArgConst(int i, size_t size, void* data, std::string name)
	{
		cl_int err;

		if (!data)
			return;

		if (!valid(name))
			return;

		cl_kernel kl;

		if (name.empty())
			kl = kernel_.begin()->second;
		else
			kl = kernel_[name];

		err = clSetKernelArg(kl, i, size, data);
		if (err != CL_SUCCESS)
			return;
	}

	void KernelProgram::setKernelArgBuf(int i, cl_mem_flags flag, size_t size, void* data, std::string name)
	{
		cl_int err;

		if (!valid(name))
			return;

		cl_kernel kl;

		if (name.empty())
			kl = kernel_.begin()->second;
		else
			kl = kernel_[name];

		if (data)
		{
			Argument arg;
			arg.index = i;
			arg.size = size;
			arg.texture = 0;
			arg.buf_src = data;
			unsigned int index;

			if (matchArg(&arg, index))
			{
				arg.buffer = arg_list_[index].buffer;
			}
			else
			{
				cl_mem buffer = clCreateBuffer(context_, flag, size, data, &err);
				if (err != CL_SUCCESS)
					return;
				arg.buffer = buffer;
				arg_list_.push_back(arg);
			}
			err = clSetKernelArg(kl, i, sizeof(cl_mem), &(arg.buffer));
			if (err != CL_SUCCESS)
				return;
		}
		else
		{
			err = clSetKernelArg(kl, i, size, NULL);
			if (err != CL_SUCCESS)
				return;
		}
	}

	void KernelProgram::setKernelArgBufWrite(int i, cl_mem_flags flag, size_t size, void* data, std::string name)
	{
		cl_int err;

		if (!valid(name))
			return;

		cl_kernel kl;

		if (name.empty())
			kl = kernel_.begin()->second;
		else
			kl = kernel_[name];

		if (data)
		{
			Argument arg;
			arg.index = i;
			arg.size = size;
			arg.texture = 0;
			arg.buf_src = data;
			unsigned int index;

			if (matchArg(&arg, index))
			{
				arg.buffer = arg_list_[index].buffer;
				clReleaseMemObject(arg_list_[index].buffer);
				arg.buffer = clCreateBuffer(context_, flag, size, data, &err);
				if (err != CL_SUCCESS)
					return;
			}
			else
			{
				cl_mem buffer = clCreateBuffer(context_, flag, size, data, &err);
				if (err != CL_SUCCESS)
					return;
				arg.buffer = buffer;
				arg_list_.push_back(arg);
			}

			err = clSetKernelArg(kl, i, sizeof(cl_mem), &(arg.buffer));
			if (err != CL_SUCCESS)
				return;
		}
		else
		{
			err = clSetKernelArg(kl, i, size, NULL);
			if (err != CL_SUCCESS)
				return;
		}
	}

	void KernelProgram::setKernelArgTex2D(int i, cl_mem_flags flag, GLuint texture, std::string name)
	{
		cl_int err;

		if (!valid(name))
			return;

		cl_kernel kl;

		if (name.empty())
			kl = kernel_.begin()->second;
		else
			kl = kernel_[name];

		Argument arg;
		arg.index = i;
		arg.size = 0;
		arg.texture = texture;
		arg.buf_src = 0;
		unsigned int index;

		if (matchArg(&arg, index))
		{
			arg.buffer = arg_list_[index].buffer;
		}
		else
		{
			cl_mem tex_buffer = clCreateFromGLTexture2D(context_, flag, GL_TEXTURE_2D, 0, texture, &err);
			if (err != CL_SUCCESS)
				return;
			arg.buffer = tex_buffer;
			arg_list_.push_back(arg);
		}
		err = clSetKernelArg(kl, i, sizeof(cl_mem), &(arg.buffer));
		if (err != CL_SUCCESS)
			return;
	}

	void KernelProgram::setKernelArgTex3D(int i, cl_mem_flags flag, GLuint texture, std::string name)
	{
		cl_int err;

		if (!valid(name))
			return;

		cl_kernel kl;

		if (name.empty())
			kl = kernel_.begin()->second;
		else
			kl = kernel_[name];

		Argument arg;
		arg.index = i;
		arg.size = 0;
		arg.texture = texture;
		arg.buf_src = 0;
		unsigned int index;

		if (matchArg(&arg, index))
		{
			arg.buffer = arg_list_[index].buffer;
		}
		else
		{
			cl_mem tex_buffer = clCreateFromGLTexture3D(context_, flag, GL_TEXTURE_3D, 0, texture, &err);
			if (err != CL_SUCCESS)
				return;
			arg.buffer = tex_buffer;
			arg_list_.push_back(arg);
		}
		err = clSetKernelArg(kl, i, sizeof(cl_mem), &(arg.buffer));
		if (err != CL_SUCCESS)
			return;
	}

	void KernelProgram::readBuffer(void* data)
	{
		bool found = false;
		unsigned int i;
		for (i=0; i<arg_list_.size(); ++i)
		{
			if (arg_list_[i].buf_src == data)
			{
				found = true;
				break;
			}
		}
		if (found)
		{
			Argument arg = arg_list_[i];
			cl_int err;
			err = clEnqueueReadBuffer(queue_, arg.buffer, CL_TRUE, 0, arg.size, data, 0, NULL, NULL);
			if (err != CL_SUCCESS)
				return;
		}
	}

	void KernelProgram::writeBuffer(void *buf_ptr, void* pattern,
		size_t pattern_size, size_t offset, size_t size)
	{
		bool found = false;
		unsigned int i;
		for (i=0; i<arg_list_.size(); ++i)
		{
			if (arg_list_[i].buf_src == buf_ptr)
			{
				found = true;
				break;
			}
		}
		if (found)
		{
			//not supported for 1.1
			Argument arg = arg_list_[i];
			cl_int err;
			err = clEnqueueFillBuffer(queue_, arg.buffer, pattern,
				pattern_size, offset, size, 0, NULL, NULL);
			if (err != CL_SUCCESS)
				return;
		}
	}

	void KernelProgram::readBuffer(int index, void* data)
	{
		bool found = false;
		unsigned int i;
		for (i=0; i<arg_list_.size(); ++i)
		{
			if (arg_list_[i].index == index)
			{
				found = true;
				break;
			}
		}
		if (found)
		{
			Argument arg = arg_list_[i];
			cl_int err;
			err = clEnqueueReadBuffer(queue_, arg.buffer, CL_TRUE, 0, arg.size, data, 0, NULL, NULL);
			if (err != CL_SUCCESS)
				return;
		}
	}

	void KernelProgram::writeBuffer(int index, void* pattern,
		size_t pattern_size, size_t offset, size_t size)
	{
		bool found = false;
		unsigned int i;
		for (i=0; i<arg_list_.size(); ++i)
		{
			if (arg_list_[i].index == index)
			{
				found = true;
				break;
			}
		}
		if (found)
		{
			//not supported for 1.1
			Argument arg = arg_list_[i];
			cl_int err;
			err = clEnqueueFillBuffer(queue_, arg.buffer, pattern,
				pattern_size, offset, size, 0, NULL, NULL);
			if (err != CL_SUCCESS)
				return;
		}
	}

	std::string& KernelProgram::getInfo()
	{
		return info_;
	}
}