/*
   QTFFmpegWrapper - QT FFmpeg Wrapper Class
   Copyright (C) 2009-2012:
         Daniel Roggen, droggen@gmail.com

   All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

   1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY COPYRIGHT HOLDERS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE FREEBSD PROJECT OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef __QVideoEncoder_H
#define __QVideoEncoder_H

#include <string>
#include <stdint.h>
#include <cstdio>
#include <iostream>

extern "C" {

#include <libavutil/avassert.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/mathematics.h>
#include <libavutil/timestamp.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>

}

#ifdef LINUX
#ifdef av_err2str
#undef av_err2str
#include <string>
av_always_inline std::string av_err2string(int errnum) {
    char str[AV_ERROR_MAX_STRING_SIZE];
    return av_make_error_string(str, AV_ERROR_MAX_STRING_SIZE, errnum);
}
#define av_err2str(err) av_err2string(err).c_str()
#endif  // av_err2str
#endif

#define STREAM_PIX_FMT    AV_PIX_FMT_YUV420P /* default pix_fmt */
#define SCALE_FLAGS SWS_BICUBIC

class QVideoEncoder
{
protected:
	//video basics
	size_t width_, height_, bitrate_, gop_, fps_;
	//the original dimensions (video is cropped so width and height are both
	//divisible by 16.)
	size_t actual_width_, actual_height_;
	std::string filename_;
	bool valid_;

	// a wrapper around a single output AVStream
	typedef struct OutputStream {
		AVStream *st;
    	AVCodecContext *enc;
    	/* pts of the next frame that will be generated */
    	int64_t next_pts;
    	int samples_count;
    	AVFrame *frame;
    	AVFrame *tmp_frame;
    	AVPacket *tmp_pkt;
    	float t, tincr, tincr2;
    	struct SwsContext *sws_ctx;
    	struct SwrContext *swr_ctx;
	} OutputStream;

	//codec and format details.
	OutputStream output_stream_;
	const AVOutputFormat *format_;
	AVFormatContext *format_context_;
	const AVCodec *video_codec_;

	//interior functions
	bool add_stream();
	bool open_video();
	AVFrame * alloc_frame();
	AVFrame * get_video_frame();
	AVFrame * get_video_frame(OutputStream *ost);
	bool write_frame(AVFormatContext *fmt_ctx, AVCodecContext *c, AVStream *st, AVFrame *frame, AVPacket *pkt);
	void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt);
public:
    QVideoEncoder();
    virtual ~QVideoEncoder();
	bool open(std::string, size_t w, size_t h, size_t fps, size_t bitrate);
	void close();
	void fill_yuv_image(AVFrame *pict, int frame_index, int width, int height);
	bool write_video_frame(size_t frame_num);
	bool write_video_frame(AVFormatContext *oc, OutputStream *ost);
	bool set_frame_rgb_data(unsigned char * data);
};




#endif // QVideoEncoder_H
