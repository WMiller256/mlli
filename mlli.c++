/* mlli.c++
 *
 * William Miller
 * Oct 14 2020
 *
 * Machine-Learned Lucky Imaging for enhancing planetary imaging.
 *
 */

#include "mlli.h"

int main(int argn, char** argv) {
    std::vector<std::string> files;
    size_t nframes;

   	po::options_description description("Usage");
	try {
		description.add_options()
			("videos,v", po::value<std::vector<std::string>>()->multitoken(), "The input video files. (Required)")
			("nframes,n", po::value<size_t>(&nframes)->default_value(-1), "The number of frames to combine. Defaults to 50% of total. (Optional)")
		;
	}
	catch (...) {
		std::cout << "Error in boost program options initialization." << std::endl;
		exit(0);
	}

  	po::variables_map vm;
    try {
    	po::store(po::command_line_parser(argn, argv).options(description).run(), vm);
    	po::notify(vm);
    }
    catch (...) {
        std::cout << description << std::endl;
        exit(1);
    }

	if (vm.count("videos")) files = vm["videos"].as<std::vector<std::string> >();
	else {
		std::cout << description << std::endl;
		exit(2);
	}

    for (auto const &file : files) {
	    std::vector<cv::Mat> frames = extract_frames(file);
	    cv::Mat coadded = coadd(frames);
	    cv::imshow("coadded", coadded);
	    cv::waitKey();
	}
}


std::vector<cv::Mat> extract_frames(const std::string &video) {

    std::vector<cv::Mat> frames;

    // ffmpeg API objects
    AVFormatContext* informat_ctx = NULL;
    AVFrame* frame = NULL;
    AVFrame* decframe = NULL;
    AVCodecContext* inav_ctx = NULL;
    AVCodec* in_codec = NULL;

    int valid_frame = 0;
    int video_stream = -1;
    int ret;
    int nframe = 0;

    // Allocation and registry
    frame = av_frame_alloc();
    decframe = av_frame_alloc();
    av_register_all();
    avcodec_register_all();
    
    if ((ret = avformat_open_input(&informat_ctx, video.c_str(), NULL, NULL)) < 0) {
    	// If opening fails print error message and exit
    	char errbuf[1024];
    	av_strerror(ret, errbuf, 1024);
        std::cout << "Could not open file "+yellow << video << res+": " << errbuf << std::endl;
        exit(-1);
    }

    // Read the meta data 
    ret = avformat_find_stream_info(informat_ctx, 0);
    if (ret < 0) {
        std::cout << red << "Failed to read input file information. " << res << std::endl;
        exit(-1);
    }

    for(int ii = 0; ii < informat_ctx->nb_streams; ii ++) {
        if (informat_ctx->streams[ii]->codec->codec_type == AVMEDIA_TYPE_VIDEO && video_stream < 0) {
            video_stream = ii;
        }
    }
    if (video_stream == -1) {
        std::cout << "Could not find stream index." << std::endl;
        exit(-1);
    }

    in_codec = avcodec_find_decoder(informat_ctx->streams[video_stream]->codec->codec_id);
    if (in_codec == NULL) {
        std::cout << "Could not find codec: " << avcodec_get_name(informat_ctx->streams[video_stream]->codec->codec_id) << std::endl;
        exit(1);
    }
    inav_ctx = informat_ctx->streams[video_stream]->codec;

    avcodec_open2(inav_ctx, in_codec, NULL);                        // Open the input codec

    std::vector<uint8_t> framebuf(avpicture_get_size(inav_ctx->pix_fmt, inav_ctx->width, inav_ctx->height));
                   
    struct SwsContext *img_convert_ctx;
    size_t nframes = informat_ctx->streams[video_stream]->nb_frames;
    frames.reserve(nframes);
    size_t current(0);
    while (ret == 0) {
        // Packet initialization
        AVPacket pkt;
        av_init_packet(&pkt);

        // Print progress
        print_percent(current++, nframes);

        ret = av_read_frame(informat_ctx, &pkt);                     // Read the next frame in
        avcodec_decode_video2(inav_ctx, frame, &valid_frame, &pkt);  // Decode the next frame
        if (!valid_frame) continue;                                  // Ignore invalid frames

        // Frame extraction
        if (ret == 0) {                                              
            img_convert_ctx = sws_getContext(inav_ctx->width, inav_ctx->height, AV_PIX_FMT_BGR24, inav_ctx->width, inav_ctx->height, AV_PIX_FMT_BGR24, 
                                             SWS_BICUBIC, NULL, NULL, NULL);
            sws_scale(img_convert_ctx, decframe->data, decframe->linesize, 0, decframe->height, frame->data, frame->linesize);
            sws_freeContext(img_convert_ctx);

            cv::Mat _frame(frame->height, frame->width, CV_64FC3, framebuf.data());
            frames.push_back(_frame);                                // And add it to the output vector
        }

        // DEBUG
        if (current > 1000) break;
    }
    std::cout << std::endl;
    ret = avcodec_close(inav_ctx);
    
    return frames;
}

cv::Mat coadd(const std::vector<cv::Mat> &frames) {
    if (frames.empty()) return cv::Mat();

    std::cout << "In coadd:" << std::endl;

    // Create a 0 initialized image to use as accumulator
    cv::Mat m(frames[0].rows, frames[0].cols, CV_64FC3);
    m.setTo(cv::Scalar(0, 0, 0, 0));

    // Initialize output
    cv::Mat out(m.rows, m.cols, CV_64FC3);

    std::cout << "Accumulating..." << std::endl;

    const size_t nframes = frames.size();
    size_t current(0);
    print_percent(current, nframes);

    cv::Mat temp(m.rows, m.cols, CV_64FC3);
    for (auto fr : frames) {
        fr.convertTo(temp, CV_64FC3);
        m += temp;
        print_percent(current++, nframes);
    }

    std::cout << "Dividing..." << std::flush;
    m.convertTo(out, CV_16UC3, 1.0 / nframes);
    std::cout << bright+green+"done"+res+"." << std::endl;

    return out;
}
