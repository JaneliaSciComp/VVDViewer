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
#include "h5j_reader.h"

extern "C" {
	#include <libavdevice/avdevice.h>
}

#include "hdf5_hl.h"
#include "H5Cpp.h"

#include "../compatibility.h"

H5JReader::H5JReader()
{
	m_resize_type = 0;
	m_resample_type = 0;
	m_alignment = 0;

	m_slice_seq = false;
	m_time_num = 0;
	m_cur_time = -1;
	m_chan_num = 0;
	m_slice_num = 0;
	m_x_size = 0;
	m_y_size = 0;

	m_valid_spc = false;
	m_xspc = 1.0;
	m_yspc = 1.0;
	m_zspc = 1.0;

	m_max_value = 0.0;
	m_scalar_scale = 1.0;

	m_batch = false;
	m_cur_batch = -1;

	m_paddingR = 0;
	m_paddingB = 0;

	m_enable_4d_seq = false;
}

H5JReader::~H5JReader()
{
	
}

void H5JReader::SetFile(string &file)
{
	if (!file.empty())
	{
		if (!m_path_name.empty())
			m_path_name.clear();
		m_path_name.assign(file.length(), L' ');
		copy(file.begin(), file.end(), m_path_name.begin());
	}
	m_id_string = m_path_name;
}

void H5JReader::SetFile(wstring &file)
{
	m_path_name = file;
	m_id_string = m_path_name;
}

void H5JReader::Preprocess()
{
	m_4d_seq.clear();

	wstring path, name;
	if (!SEP_PATH_NAME(m_path_name, path, name))
		return;//READER_OPEN_FAIL;

	m_data_name = name;

	std::vector<std::wstring> list;
	if (!m_enable_4d_seq || !FIND_FILES_4D(m_path_name, m_time_id, list, m_cur_time))
	{
		TimeDataInfo info;
		info.stack = path + name;  //temporary name
		info.filenumber = 0;
		m_4d_seq.push_back(info);
		m_cur_time = 0;
	}
	else
	{
		int64_t begin = m_path_name.find(m_time_id);
		size_t id_len = m_time_id.length();
		for (size_t i = 0; i < list.size(); i++) {
			TimeDataInfo info;
			std::wstring str = list.at(i);
			std::wstring t_num;
			for (size_t j = begin + id_len; j < str.size(); j++)
			{
				wchar_t c = str[j];
				if (iswdigit(c))
					t_num.push_back(c);
				else break;
			}
			if (t_num.size() > 0)
				info.filenumber = WSTOI(t_num);
			else
				info.filenumber = 0;
			info.stack = str;
			m_4d_seq.push_back(info);
		}
	}
	if (m_4d_seq.size() > 1)
		std::sort(m_4d_seq.begin(), m_4d_seq.end(), H5JReader::h5j_sort);

	//get time number
	m_time_num = (int)m_4d_seq.size();
	
	H5::Exception::dontPrint();
	H5::H5File *file = new H5::H5File(ws2s(m_path_name), H5F_ACC_RDONLY);
	if (file == NULL) return;

	for (size_t i = 0; i < file->getObjCount(); i++)
	{
		H5std_string name = file->getObjnameByIdx(i);
		if (name == "Channels")
		{
			H5::Group channels = file->openGroup(name);
			long data[2];
			double vx_size[3];
			char string_out[80];

			H5::Attribute attr = 0;
			H5::DataType type = 0;
			if (channels.attrExists("width")) {
				H5::Attribute attr = channels.openAttribute("width");
				H5::DataType type = attr.getDataType();
				// Windows throws a runtime error about a corrupted stack if we
				// try to read the attribute directly into the variable
				attr.read(type, data);
				attr.close();
				m_x_size = data[0];
			}

			if (channels.attrExists("height")) {
				attr = channels.openAttribute("height");
				type = attr.getDataType();
				attr.read(type, data);
				attr.close();
				m_y_size = data[0];
			}

			if (channels.attrExists("pad_right")) {
				attr = channels.openAttribute("pad_right");
				type = attr.getDataType();
				attr.read(type, data);
				attr.close();
				m_paddingR = data[0];
			}

			if (channels.attrExists("pad_bottom")) {
				attr = channels.openAttribute("pad_bottom");
				type = attr.getDataType();
				attr.read(type, data);
				attr.close();
				m_paddingB = data[0];
			}

			if (channels.attrExists("frames")) {
				attr = channels.openAttribute("frames");
				type = attr.getDataType();
				attr.read(type, data);
				attr.close();
				m_slice_num = data[0];
			}

			if (file->attrExists("voxel_size")) {
				attr = file->openAttribute("voxel_size");
				type = attr.getDataType();
				attr.read(type, vx_size);
				attr.close();
				m_xspc = vx_size[0];
				m_yspc = vx_size[1];
				m_zspc = vx_size[2];
				m_valid_spc = true;
			}
			/*
			if (file->attrExists("unit")) {
				attr = file->openAttribute("unit");
				type = attr.getDataType();
				attr.read(type, string_out);
				attr.close();
				m_unit = s2ws(string_out);
			}
			*/
			// Count the number of channels
			m_chan_num = 0;
			for (size_t obj = 0; obj < channels.getNumObjs(); obj++) {
				if (channels.getObjTypeByIdx(obj) == H5G_DATASET) {
					m_chan_num++;
				}
			}

			channels.close();
		}
	}

	delete file;


	return;// READER_OK;
}

uint16_t H5JReader::SwapShort(uint16_t num) {
	return ((num & 0x00FF) << 8) | ((num & 0xFF00) >> 8);
}

uint32_t H5JReader::SwapWord(uint32_t num) {
	return ((num & 0x000000FF) << 24) | ((num & 0xFF000000) >> 24) |
		((num & 0x0000FF00) << 8) | ((num & 0x00FF0000) >> 8);
}

uint64_t H5JReader::SwapLong(uint64_t num) {
	return
		((num & 0x00000000000000FF) << 56) |
		((num & 0xFF00000000000000) >> 56) |
		((num & 0x000000000000FF00) << 40) |
		((num & 0x00FF000000000000) >> 40) |
		((num & 0x0000000000FF0000) << 24) |
		((num & 0x0000FF0000000000) >> 24) |
		((num & 0x00000000FF000000) << 8) |
		((num & 0x000000FF00000000) >> 8);
}

void H5JReader::SetSliceSeq(bool ss)
{
	//enable searching for slices
	m_slice_seq = ss;
}

bool H5JReader::GetSliceSeq()
{
	return m_slice_seq;
}

void H5JReader::SetTimeId(wstring &id)
{
	m_time_id = id;
}

wstring H5JReader::GetTimeId()
{
	return m_time_id;
}

bool H5JReader::GetTimeSeq()
{
	return m_enable_4d_seq;
}

void H5JReader::SetTimeSeq(bool ts)
{
	m_enable_4d_seq = ts;
}


void H5JReader::SetBatch(bool batch)
{
	if (batch)
	{
		//separate path and name
		wstring search_path = GET_PATH(m_path_name);
		wstring suffix = GET_SUFFIX(m_path_name);
		FIND_FILES(search_path, suffix, m_batch_list, m_cur_batch);
		m_batch = true;
	}
	else
		m_batch = false;
}

int H5JReader::LoadBatch(int index)
{
	int result = -1;
	if (index >= 0 && index < (int)m_batch_list.size())
	{
		m_path_name = m_batch_list[index];
		Preprocess();
		result = index;
		m_cur_batch = result;
	}
	else
		result = -1;

	return result;
}

Nrrd* H5JReader::ConvertNrrd(int t, int c, bool get_max)
{
	if (t < 0 || t >= m_time_num ||
		c < 0 || c >= m_chan_num)
		return 0;

	Nrrd* data = nrrdNew();
	void *val = 0;
	bool eight_bit = true;

	H5::Exception::dontPrint();
	H5::H5File *file = new H5::H5File(ws2s(m_4d_seq[t].stack), H5F_ACC_RDONLY);
	if (file == NULL) return NULL;

	for (size_t i = 0; i < file->getObjCount(); i++)
	{
		H5std_string name = file->getObjnameByIdx(i);
		if (name == "Channels")
		{
			H5::Group channels = file->openGroup(name);

			int channel_idx = 0;

			for (size_t obj = 0; obj < channels.getNumObjs(); obj++)
			{
				if (channels.getObjTypeByIdx(obj) == H5G_DATASET)
				{
					if (channel_idx == c) {
						H5std_string ds_name = channels.getObjnameByIdx(obj);
						H5::DataSet data = channels.openDataSet(ds_name);
						uint8_t* channel_buf = new uint8_t[data.getStorageSize()];
						data.read(channel_buf, data.getDataType());

						FFMpegMemDecoder decoder((void *)channel_buf, data.getStorageSize(), m_paddingR, m_paddingB);
						decoder.start();
						eight_bit = decoder.getBytesPerPixel() > 1 ? false : true;
						val = decoder.grab();
						decoder.close();

						break;
					}
					channel_idx++;
				}
			}

			channels.close();
			break;
		}
	}

	delete file;

	if (val == NULL) return NULL;
	
	//write to nrrd
	if (eight_bit)
		nrrdWrap(data, (uint8_t*)val, nrrdTypeUChar,
			3, (size_t)m_x_size, (size_t)m_y_size, (size_t)m_slice_num);
	else
		nrrdWrap(data, (uint16_t*)val, nrrdTypeUShort,
			3, (size_t)m_x_size, (size_t)m_y_size, (size_t)m_slice_num);
	nrrdAxisInfoSet(data, nrrdAxisInfoSpacing, m_xspc, m_yspc, m_zspc);
	nrrdAxisInfoSet(data, nrrdAxisInfoMax, m_xspc*m_x_size,
		m_yspc*m_y_size, m_zspc*m_slice_num);
	nrrdAxisInfoSet(data, nrrdAxisInfoMin, 0.0, 0.0, 0.0);
	nrrdAxisInfoSet(data, nrrdAxisInfoSize, (size_t)m_x_size,
		(size_t)m_y_size, (size_t)m_slice_num);

	if (!eight_bit) {
		unsigned short value;
		unsigned long long totali = (unsigned long long)m_slice_num*
			(unsigned long long)m_x_size*(unsigned long long)m_y_size;
		for (unsigned long long i = 0; i < totali; ++i)
		{
			value = ((unsigned short*)data->data)[i] >> 4;
			((unsigned short*)data->data)[i] = value;
			m_max_value = value > m_max_value ? value : m_max_value;
		}
		if (!get_max) m_max_value = 0.0;
		if (m_max_value > 0.0) m_scalar_scale = 65535.0 / m_max_value;
		else m_scalar_scale = 1.0;
	}
	else m_max_value = 255.0;
	
	m_cur_time = t;

	return data;
}

wstring H5JReader::GetCurDataName(int t, int c)
{
	if (t >= 0 && t < (int64_t)m_4d_seq.size())
		return m_4d_seq[t].stack;
	else
		return L"";
}

wstring H5JReader::GetCurMaskName(int t, int c)
{
	if (t >= 0 && t < (int64_t)m_4d_seq.size())
	{
		wstring data_name = m_4d_seq[t].stack;
		wostringstream woss;
		woss << data_name.substr(0, data_name.find_last_of('.'));
		if (m_chan_num > 1) woss << "_C" << c;
		woss << ".msk";
		wstring mask_name = woss.str();
		return mask_name;
	}
	else
		return L"";
}

wstring H5JReader::GetCurLabelName(int t, int c)
{
	if (t >= 0 && t < (int64_t)m_4d_seq.size())
	{
		wstring data_name = m_4d_seq[t].stack;
		std::wostringstream woss;
		woss << data_name.substr(0, data_name.find_last_of('.'));
		if (m_chan_num > 1) woss << "_C" << c;
		woss << ".lbl";
		wstring label_name = woss.str();
		return label_name;
	}
	else
		return L"";
}

wstring H5JReader::GetCurName(int t, int c)
{
	if (t >= 0 && t < (int64_t)m_4d_seq.size())
		return m_4d_seq[t].stack;
	else
		return L"";
}

bool H5JReader::h5j_sort(const TimeDataInfo& info1, const TimeDataInfo& info2)
{
	return info1.filenumber < info2.filenumber;
}



H5JMemStream::H5JMemStream(unsigned char *data, size_t datasize) {
	m_data = data;
	m_datasize = datasize;
	m_pos = 0;
}

void H5JMemStream::seek(size_t pos) {
	if (pos >= 0 && pos < m_datasize)
		m_pos = pos;
}

size_t H5JMemStream::read(unsigned char *buf, size_t len) {
	int readbyte = 0;;

	if (buf != NULL && m_data != NULL) {
		readbyte = (len <= m_datasize - m_pos) ? len : m_datasize - m_pos;
		if (readbyte > 0) {
			memcpy(buf, m_data + m_pos, readbyte);
			m_pos += readbyte;
		} else
			readbyte = 0;
	}

	return readbyte;
}


extern "C" {

	int readFunction(void* opaque, uint8_t* buf, int buf_size)
	{
		H5JMemStream* stream = (H5JMemStream*) opaque;
		int numBytes = stream->read((unsigned char*)buf, buf_size);
		return numBytes;
	}

}


FFMpegMemDecoder::FFMpegMemDecoder(void *ibytes, size_t isize, size_t paddingR, size_t paddingB, int frame_num)
{
	_filename = wstring();
	_format_context = NULL;
	_video_stream = NULL;
	_video_codec = NULL;
	picture = NULL; picture_rgb = NULL;
	img_convert_ctx = NULL;
	_frame_grabbed;
	_time_stamp;
	deinterlace = false;

	_frame_count = 0;
	_frame_num = frame_num;
	_flush = false;

	_buf_size = isize + AV_INPUT_BUFFER_PADDING_SIZE;
	
	_width = -1;
	_height = -1;
	_paddingR = paddingR;
	_paddingB = paddingB;

	_bytesperpixel = 0;

	_buffer = (unsigned char*)av_malloc(_buf_size);
	_memstream = new H5JMemStream((unsigned char *)ibytes, isize);

	// create format context
	_format_context = avformat_alloc_context();

	_format_context->pb = avio_alloc_context(_buffer, _buf_size, 0, _memstream, &readFunction, NULL, NULL);
	_format_context->flags = _format_context->flags | AVFMT_FLAG_CUSTOM_IO;

	_outbuf_size = 0;
}

FFMpegMemDecoder::~FFMpegMemDecoder()
{
	close();
}

void FFMpegMemDecoder::close()
{
	av_packet_unref(&pkt);

	// Close the video codec
	if (_video_codec != NULL) {
		avcodec_free_context(&_video_codec);
		_video_codec = NULL;
	}

	// Close the video file
	if (_format_context != NULL) {
		av_free(_format_context->pb->buffer);
		avio_context_free(&_format_context->pb);
		avformat_free_context(_format_context);
		_format_context = NULL;
	}

	if (img_convert_ctx != NULL) {
		sws_freeContext(img_convert_ctx);
		img_convert_ctx = NULL;
	}

	if (picture_rgb != NULL)
		av_frame_free(&picture_rgb);

	_frame_grabbed = false;
	_time_stamp = 0;

	if (_memstream != NULL) {
		delete _memstream;
		_memstream = NULL;
	}

}

AVPixelFormat FFMpegMemDecoder::getPixelFormat()
{
	AVPixelFormat result = AV_PIX_FMT_NONE;
	if (_components_per_frame == 1)
	{
		if (_bytesperpixel == 1)
			result = AV_PIX_FMT_GRAY8;
		else if (_bytesperpixel == 2)
			result = AV_PIX_FMT_GRAY16;
		else
			result = AV_PIX_FMT_NONE;
	}
	else if (_components_per_frame == 3)
	{
		result = AV_PIX_FMT_BGR24;
	}
	else if (_components_per_frame == 4)
	{
		result = AV_PIX_FMT_BGRA;
	}
	else if (_video_codec != NULL)
	{ // RAW
		result = _video_codec->pix_fmt;
	}

	return result;
}

void FFMpegMemDecoder::start()
{
	int ret;
	const char *charfname = ws2s(_filename).c_str();

	// Open video file
	AVDictionary *options = NULL;

	if ((ret = avformat_open_input(&_format_context, charfname, NULL, &options)) < 0) {
		av_dict_set(&options, "pixel_format", NULL, 0);
		if ((ret = avformat_open_input(&_format_context, charfname, NULL, &options)) < 0) {
			return;
		}
	}
	av_dict_free(&options);

	// Retrieve stream information
	if ((ret = avformat_find_stream_info(_format_context, NULL)) < 0) {
		return;
	}

	// Dump information about file onto standard error
	av_dump_format(_format_context, 0, charfname, 0);
	
#ifdef _WIN32
	AVCodec *decoder;
#else
	const AVCodec *decoder;
#endif
	ret = av_find_best_stream(_format_context, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
	if (ret < 0) {
		fprintf(stderr, "Cannot find a video stream in the input file\n");
		return;
	}
	_video_stream = _format_context->streams[ret];
	_video_codec = avcodec_alloc_context3(decoder);
	if (avcodec_parameters_to_context(_video_codec, _video_stream->codecpar) < 0)
		return;
	_video_codec->thread_count = 0;
	_video_codec->thread_type = 3;

	AVDictionary *opts = NULL;
	av_dict_set(&opts, "refcounted_frames", "1", 0);
	if ((ret = avcodec_open2(_video_codec, decoder, &opts)) < 0) {
		return;
	}
	if (_video_codec->time_base.num > 1000 && _video_codec->time_base.den == 1) {
		_video_codec->time_base.den = 1000;
	}

	if (_video_stream == NULL)
		return;

	_width = _video_codec->width;
	_height = _video_codec->height;
	_frame_num = (_video_stream->nb_frames > 0 && _frame_num <= 0) ? _video_stream->nb_frames : _frame_num;

	const AVPixFmtDescriptor *fmt = av_pix_fmt_desc_get(_video_codec->pix_fmt);
	int comps = fmt->nb_components;
	_components_per_frame = comps;
	const AVComponentDescriptor *desc = fmt->comp;
	if (desc[0].depth == 8) {
		_components_per_frame = 3; //rgb
		_bytesperpixel = 1;
	}
	else {
		_components_per_frame = 1; //gray16
		_bytesperpixel = 2;
	}

	img_convert_ctx = sws_getContext(
		_video_codec->width, _video_codec->height, _video_codec->pix_fmt,
		_video_codec->width, _video_codec->height, getPixelFormat(), SWS_BICUBIC,
		NULL, NULL, NULL);
	if (img_convert_ctx == NULL) {
		return;
	}
}

void FFMpegMemDecoder::extractBytes(unsigned char *input, int frameid, unsigned char *output)
{
	if (output == NULL)
		return;

	int width = _video_codec->width - _paddingR;
	int height = _video_codec->height - _paddingB;
	int linesize = _bytesperpixel == 1 ?
		picture_rgb->linesize[0] / 3 :
		picture_rgb->linesize[0] / 2;
	int padding = linesize - (_video_codec->width - _paddingR);
	if (padding < 0) {
		padding = 0;
	}

	if (_bytesperpixel == 1) {
		unsigned char *outputBytes = (unsigned char *)output + ((size_t)frameid*(size_t)width*(size_t)height*(size_t)_bytesperpixel);
		unsigned char *inputBytes = picture_rgb->data[0];

		long long inputOffset = 0;
		long long outputOffset = 0;
		for (int rows = 0; rows < height; rows++) {
			for (int cols = 0; cols < width; cols++) {
				outputBytes[outputOffset] = inputBytes[3 * inputOffset];
				inputOffset++;
				outputOffset++;
			}
			inputOffset += padding;
		}
	}
	else {
		unsigned char *outputBytes = (unsigned char *)output + ((size_t)frameid*(size_t)width*(size_t)height*(size_t)_bytesperpixel);
		unsigned char *inputBytes = picture_rgb->data[0];

		int inputOffset = 0;
		int outputOffset = 0;
		int colnum = width * _bytesperpixel;
		for (int rows = 0; rows < height; rows++) {
			for (int cols = 0; cols < colnum; cols++) {
				outputBytes[outputOffset] = inputBytes[inputOffset];
				inputOffset++;
				outputOffset++;
			}
			inputOffset += padding * _bytesperpixel;
		}
	}
}

void FFMpegMemDecoder::processImage(int frameid, unsigned char *output)
{
	if (picture_rgb == NULL) {
		if ((picture_rgb = av_frame_alloc()) == NULL)
			return;
		av_frame_copy_props(picture_rgb, picture);
		picture_rgb->width = getImageWidth();
		picture_rgb->height = getImageHeight();
		picture_rgb->format = getPixelFormat();
		picture_rgb->nb_samples = 0;
		av_frame_get_buffer(picture_rgb, 0);
	}

	// Convert the image from its native format to RGB or GRAY
	sws_scale(img_convert_ctx, picture->data, picture->linesize, 0,
		_video_codec->height, picture_rgb->data, picture_rgb->linesize);

	extractBytes(picture_rgb->data[0], _frame_count, output);
}

void *FFMpegMemDecoder::grab(void *out)
{
	bool done = false;
	int ret = 0;
	_frame_count = 0;
	_flush = false;
	int frame_limit = _frame_num > 0 ? _frame_num : INT_MAX;
	pkt = AVPacket();

	_outbuf_size =	(size_t)(_video_codec->width-_paddingR) *
					(size_t)(_video_codec->height-_paddingB) *
					(size_t)_frame_num *
					(size_t)getBytesPerPixel();
	if (_outbuf_size == 0)  return NULL;
	
	unsigned char *outbuf = out == NULL ? new unsigned char[_outbuf_size] : (unsigned char*)out;
	if (!outbuf)
		throw std::runtime_error("Unable to allocate memory to read a video stream.");

	while (_frame_count < frame_limit && av_read_frame(_format_context, &pkt) == 0) {
		if (pkt.stream_index == _video_stream->index) {
			if (avcodec_send_packet(_video_codec, &pkt) < 0)
				return NULL;
			while (true) {
				if ((picture = av_frame_alloc()) == NULL)
					return NULL;
				if (avcodec_receive_frame(_video_codec, picture) == 0) {
					long pts = picture->best_effort_timestamp;
					AVRational time_base = _video_stream->time_base;
					_time_stamp = 1000000L * pts * time_base.num / time_base.den;
					processImage(_frame_count, outbuf);
					av_frame_free(&picture);
					_frame_count++;
				}
				else
					break;
			}
		}
		av_packet_unref(&pkt);
	}

	//flush
	if (avcodec_send_packet(_video_codec, NULL) < 0) {
		printf("avcodec_send_packet failed");
	}
	while (true) {
		if ((picture = av_frame_alloc()) == NULL)
			return NULL;
		if (avcodec_receive_frame(_video_codec, picture) == 0) {
			long pts = picture->best_effort_timestamp;
			AVRational time_base = _video_stream->time_base;
			_time_stamp = 1000000L * pts * time_base.num / time_base.den;
			processImage(_frame_count, outbuf);
			av_frame_free(&picture);
			_frame_count++;
		}
		else
			break;
	}

	//zero fill
	if (_frame_count < _frame_num && _frame_num > 0) {
		int width = _video_codec->width - _paddingR;
		int height = _video_codec->height - _paddingB;
		size_t slicebytes = (size_t)width*(size_t)height*(size_t)_bytesperpixel;
		unsigned char *outputBytes = (unsigned char *)outbuf + (size_t)_frame_count*slicebytes;
		size_t remainings = (size_t)(_frame_num - _frame_count)*slicebytes;
		memset(outputBytes, 0, remainings * sizeof(*outputBytes));
	}
	
	return outbuf;
}
