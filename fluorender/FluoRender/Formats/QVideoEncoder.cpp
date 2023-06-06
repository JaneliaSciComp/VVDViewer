/*
 * Copyright (c) 2003 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/**
 * This class sets up all ffmpeg settings and writes frames for various video
 * containers with the YUV420 codec. You must open and close each file, checking
 * that all functions, including writing a frame, returns true for success.
 *
 * @author Brig Bagley
 * @version 1 October 2014
 * @copyright SCI Institute, Univ. of Utah 2014
 */

//FYI: FFmpeg is a pure C project using C99 math features, 
//in order to enable C++ to use them you have to append -D__STDC_CONSTANT_MACROS to your CXXFLAGS

#include "QVideoEncoder.h"

QVideoEncoder::QVideoEncoder() {
    /* Initialize libavcodec, and register all codecs and formats. */
	output_stream_.frame = 0;
	output_stream_.next_pts = 0;
	output_stream_.samples_count = 0;
	output_stream_.st = 0;
	output_stream_.swr_ctx = 0;
	output_stream_.sws_ctx = 0;
	output_stream_.t = 0;
	output_stream_.tincr = 0;
	output_stream_.tincr2 = 0;
	output_stream_.tmp_frame = 0;
	format_ = NULL;
	format_context_ = NULL;
	video_codec_ = NULL;
    filename_ = "";
	width_ = 0;
	height_ = 0;
	actual_width_ = 0;
	actual_height_ = 0;
	fps_ = 0;
	bitrate_ = 0;
	gop_ = 0;
	valid_ = false;
}

bool QVideoEncoder::open(std::string f, size_t w, size_t h, 
	size_t fps, size_t bitrate) {
	output_stream_.frame = 0;
	output_stream_.next_pts = 0;
	output_stream_.samples_count = 0;
	output_stream_.st = 0;
	output_stream_.swr_ctx = 0;
	output_stream_.sws_ctx = 0;
	output_stream_.t = 0;
	output_stream_.tincr = 0;
	output_stream_.tincr2 = 0;
	output_stream_.tmp_frame = 0;
    filename_ = f;
	// make sure width and height are divisible by 16
	actual_width_ = w;
	actual_height_ = h;
	width_ = ((size_t)(w/16))*16;
	height_ = ((size_t)(h/16))*16;
	fps_ = fps;
	bitrate_ = bitrate;
	gop_ = 12;
    avformat_alloc_output_context2(
		&format_context_, NULL, NULL, filename_.c_str());
    if (!format_context_) {
        std::cerr << "Could not deduce output" <<
			"format from file extension: using MPEG.\n";
        avformat_alloc_output_context2(
			&format_context_, NULL, "mpeg", filename_.c_str());
    }
    if (!format_context_) return false;
    format_ = format_context_->oformat;
    /* Add the audio and video streams using the default format codecs
     * and initialize the codecs. */
    if (format_->video_codec != AV_CODEC_ID_NONE) 
        if (!add_stream()) return false;
    /* Now that all the parameters are set, we can open the audio and
     * video codecs and allocate the necessary encode buffers. */
    if(!open_video()) return false;
	//av_dump_format(format_context_, 0, filename_.c_str(), 1);
    /* open the output file, if needed */
    if (!(format_->flags & AVFMT_NOFILE)) {
		int ret = avio_open(&format_context_->pb, 
			filename_.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
			fprintf(stderr, "Could not open '%s': %d\n", filename_.c_str(),ret);
            return false;
        }
    }
    /* Write the stream header, if any. */
    int ret = avformat_write_header(format_context_, NULL);
    if (ret < 0) {
        fprintf(stderr, "Error occurred when opening output file: %d\n",ret);
        return false;
    }
	valid_ = true;
	return true;
}

QVideoEncoder::~QVideoEncoder(){

}

bool QVideoEncoder::add_stream()
{
    AVCodecContext *c;
    int i;

    /* find the encoder */
    video_codec_ = avcodec_find_encoder(format_->video_codec);
    if (!video_codec_) {
        fprintf(stderr, "Could not find encoder for '%s'\n",
                avcodec_get_name(format_->video_codec));
        return false;
    }

    output_stream_.tmp_pkt = av_packet_alloc();
    if (!output_stream_.tmp_pkt) {
        fprintf(stderr, "Could not allocate AVPacket\n");
        return false;
    }

    output_stream_.st = avformat_new_stream(format_context_, NULL);
    if (!output_stream_.st) {
        fprintf(stderr, "Could not allocate stream\n");
        return false;
    }
    output_stream_.st->id = format_context_->nb_streams-1;
    c = avcodec_alloc_context3(video_codec_);
    if (!c) {
        fprintf(stderr, "Could not alloc an encoding context\n");
        return false;
    }
    output_stream_.enc = c;

    switch (video_codec_->type) {
    case AVMEDIA_TYPE_AUDIO:
        break;

    case AVMEDIA_TYPE_VIDEO:
        c->codec_id = format_->video_codec;

        c->bit_rate = bitrate_;
        /* Resolution must be a multiple of two. */
        c->width    = width_;
        c->height   = height_;
        /* timebase: This is the fundamental unit of time (in seconds) in terms
         * of which frame timestamps are represented. For fixed-fps content,
         * timebase should be 1/framerate and timestamp increments should be
         * identical to 1. */
        output_stream_.st->time_base = av_make_q( 1, fps_ );
        c->time_base       = output_stream_.st->time_base;

        c->gop_size      = gop_; /* emit one intra frame every twelve frames at most */
        c->pix_fmt       = STREAM_PIX_FMT;
        if (c->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
            /* just for testing, we also add B-frames */
            c->max_b_frames = 2;
        }
        if (c->codec_id == AV_CODEC_ID_MPEG1VIDEO) {
            /* Needed to avoid using macroblocks in which some coeffs overflow.
             * This does not happen with normal video, it just happens here as
             * the motion of the chroma plane does not match the luma plane. */
            c->mb_decision = 2;
        }
        break;

    default:
        break;
    }

    /* Some formats want stream headers to be separate. */
    if (format_context_->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    return true;
}

bool QVideoEncoder::open_video()
{
    int ret;
    AVCodecContext *c = output_stream_.enc;

    /* open the codec */
    ret = avcodec_open2(c, video_codec_, NULL);
    if (ret < 0) {
        fprintf(stderr, "Could not open video codec: %s\n", av_err2str(ret));
        return false;
    }

    /* allocate and init a re-usable frame */
    output_stream_.frame = alloc_frame();
    if (!output_stream_.frame) {
        fprintf(stderr, "Could not allocate video frame\n");
        return false;
    }

    /* If the output format is not YUV420P, then a temporary YUV420P
     * picture is needed too. It is then converted to the required
     * output format. */
    output_stream_.tmp_frame = NULL;
    if (c->pix_fmt != AV_PIX_FMT_YUV420P) {
        output_stream_.tmp_frame = alloc_frame();
        if (!output_stream_.tmp_frame) {
            fprintf(stderr, "Could not allocate temporary video frame\n");
            return false;
        }
    }

    /* copy the stream parameters to the muxer */
    ret = avcodec_parameters_from_context(output_stream_.st->codecpar, c);
    if (ret < 0) {
        fprintf(stderr, "Could not copy the stream parameters\n");
        return false;
    }

    return true;
}

AVFrame *QVideoEncoder::alloc_frame()
{
    AVFrame *frame;
    int ret;

    frame = av_frame_alloc();
    if (!frame)
        return NULL;

    frame->format = AV_PIX_FMT_YUV420P;
    frame->width  = width_;
    frame->height = height_;

    /* allocate the buffers for the frame data */
    ret = av_frame_get_buffer(frame, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate frame data.\n");
        return NULL;
    }
    return frame;
}

void QVideoEncoder::close() {
	if (!valid_) return;
	//flush the remaining (delayed) frames.
    int encode_video = 0;
    while (encode_video) {
        encode_video = !write_video_frame(format_context_, &output_stream_);
    }

    av_write_trailer(format_context_);

    /* Close each codec. */
    avcodec_free_context(&output_stream_.enc);
    av_frame_free(&output_stream_.frame);
    av_frame_free(&output_stream_.tmp_frame);
    av_packet_free(&output_stream_.tmp_pkt);
    sws_freeContext(output_stream_.sws_ctx);
    swr_free(&output_stream_.swr_ctx);

    if (!(format_context_->oformat->flags & AVFMT_NOFILE))
        /* Close the output file. */
        avio_closep(&format_context_->pb);

    /* free the stream */
    avformat_free_context(format_context_);
    valid_ = false;
}

void QVideoEncoder::log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt) {
    AVRational *time_base = 
		&fmt_ctx->streams[pkt->stream_index]->time_base;
    std::cout << "pts: " << pkt->pts <<
				 "\npts_time: " << (pkt->pts * 
				 (double)time_base->num / time_base->den) <<
				 "\ndts: " << pkt->dts <<
				 "\ndts_time: " << (pkt->dts * 
				 (double)time_base->num / time_base->den) <<
				 "\nduration: " << pkt->duration <<
				 "\nduration_time: " << (pkt->duration * 
				 (double)time_base->num / time_base->den) <<
				 "\nstream_index: " << pkt->stream_index << std::endl;
}

bool QVideoEncoder::write_video_frame(AVFormatContext *oc, OutputStream *ost) {
    return write_frame(oc, ost->enc, ost->st, get_video_frame(ost), ost->tmp_pkt);
}

bool QVideoEncoder::write_video_frame(size_t frame_num) {
    int ret;
    AVCodecContext *c;
    int got_packet = 0;
	c = output_stream_.enc;
	output_stream_.frame->pts = frame_num;
	output_stream_.next_pts = frame_num+1;
    return write_video_frame(format_context_, &output_stream_);
}

AVFrame *QVideoEncoder::get_video_frame(OutputStream *ost)
{
    AVCodecContext *c = ost->enc;

    /* when we pass a frame to the encoder, it may keep a reference to it
     * internally; make sure we do not overwrite it here */
    if (av_frame_make_writable(ost->frame) < 0)
        return NULL;

    if (c->pix_fmt != AV_PIX_FMT_YUV420P) {
        /* as we only generate a YUV420P picture, we must convert it
         * to the codec pixel format if needed */
        if (!ost->sws_ctx) {
            ost->sws_ctx = sws_getContext(c->width, c->height,
                                          AV_PIX_FMT_YUV420P,
                                          c->width, c->height,
                                          c->pix_fmt,
                                          SCALE_FLAGS, NULL, NULL, NULL);
            if (!ost->sws_ctx) {
                fprintf(stderr,
                        "Could not initialize the conversion context\n");
                exit(1);
            }
        }
        //fill_yuv_image(ost->tmp_frame, ost->next_pts, c->width, c->height);
        sws_scale(ost->sws_ctx, (const uint8_t * const *) ost->tmp_frame->data,
                  ost->tmp_frame->linesize, 0, c->height, ost->frame->data,
                  ost->frame->linesize);
    } else {
        //fill_yuv_image(ost->frame, ost->next_pts, c->width, c->height);
    }

    ost->frame->pts = ost->next_pts++;

    return ost->frame;
}


AVFrame * QVideoEncoder::get_video_frame() {
    return get_video_frame(&output_stream_);
}

bool QVideoEncoder::write_frame(AVFormatContext *fmt_ctx, AVCodecContext *c, AVStream *st, AVFrame *frame, AVPacket *pkt)
{
    int ret;

    // send the frame to the encoder
    ret = avcodec_send_frame(output_stream_.enc, frame);
    if (ret < 0) {
        fprintf(stderr, "Error sending a frame to the encoder: %s\n",
                av_err2str(ret));
        return false;
    }

    while (ret >= 0) {
        ret = avcodec_receive_packet(c, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        else if (ret < 0) {
            fprintf(stderr, "Error encoding a frame: %s\n", av_err2str(ret));
            return false;
        }

        /* rescale output packet timestamp values from codec to stream timebase */
        av_packet_rescale_ts(pkt, c->time_base, st->time_base);
        pkt->stream_index = st->index;

        /* Write the compressed frame to the media file. */
        log_packet(fmt_ctx, pkt);
        ret = av_interleaved_write_frame(fmt_ctx, pkt);
        /* pkt is now blank (av_interleaved_write_frame() takes ownership of
         * its contents and resets pkt), so that no unreferencing is necessary.
         * This would be different if one used av_write_frame(). */
        if (ret < 0) {
            fprintf(stderr, "Error while writing output packet: %s\n", av_err2str(ret));
            return false;
        }
    }

    return ret == AVERROR_EOF ? 1 : 0;
}

/* Prepare a dummy image. */
void QVideoEncoder::fill_yuv_image(AVFrame *pict, int frame_index, int width, int height)
{
    int x, y, i;
    int ret;
    /* when we pass a frame to the encoder, it may keep a reference to it
     * internally;
     * make sure we do not overwrite it here
     */
    ret = av_frame_make_writable(pict);
    if (ret < 0)
        return;

    i = frame_index;

    /* Y */
    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++)
            pict->data[0][y * pict->linesize[0] + x] = x + y + i * 3;

    /* Cb and Cr */
    for (y = 0; y < height / 2; y++) {
        for (x = 0; x < width / 2; x++) {
            pict->data[1][y * pict->linesize[1] + x] = 128 + y + i * 2;
            pict->data[2][y * pict->linesize[2] + x] = 64 + x + i * 5;
        }
    }
}

//main method to set the RGB data.
bool QVideoEncoder::set_frame_rgb_data(unsigned char * data) {
	/* as we only generate a YUV420P picture, we must convert it
		* to the codec pixel format if needed */
	if (!output_stream_.sws_ctx) {
		output_stream_.sws_ctx = sws_getContext(
			width_, height_,AV_PIX_FMT_RGB24,
			width_, height_, AV_PIX_FMT_YUV420P, 
			SCALE_FLAGS, NULL, NULL, NULL);
		if (!output_stream_.sws_ctx) {
			fprintf(stderr, "Could not initialize the conversion context\n");
			return false;
		}
	}
	unsigned char * temp_array = new unsigned char[width_ * height_ * 3 + 16];
	unsigned char * aligned_data = &temp_array[0];
	//align the data
	if ((uintptr_t)data &15) {
		while((uintptr_t)aligned_data &15)
			aligned_data++;
	}
	//this is where data gets cropped if width/height are not divisible by 16
	for(size_t i = 0; i < height_; i++)
		memcpy(
		aligned_data + (3 * i * width_),
		data         + (3 * i * actual_width_),
		3 * width_);
	//now convert the pixel format
	uint8_t * inData[] = { aligned_data }; // RGB24 have one plane
    int inLinesize[] = { (int)(3*width_) }; // RGB stride
    int ret = av_frame_make_writable(output_stream_.frame);
    if (ret < 0)
        return false;
	sws_scale(output_stream_.sws_ctx,
				inData, inLinesize,
				0, height_, output_stream_.frame->data, 
				output_stream_.frame->linesize);
	delete temp_array;
	return true;
}
