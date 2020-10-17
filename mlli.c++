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
    float superres;

   	po::options_description description("Usage");
	try {
		description.add_options()
			("videos,v", po::value<std::vector<std::string>>()->multitoken(), "The input video files. (Required)")
			("nframes,n", po::value<size_t>(&nframes)->default_value(-1), "The number of frames to combine. Defaults to 50% of total. (Optional)")
			("superres,s", po::value<float>(&superres)->default_value(2.3), "The scale factor to apply in super-resolution generation. (Optional)")
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
	    std::vector<cv::Mat> frames = extract_frames(file, superres);
	    cv::Mat coadded = coadd(frames);
	    coadded.convertTo(coadded, CV_8UC3);
        cv::imshow("coadded", coadded);
	    unsharpMask(coadded, 1, 12);
	    cv::imshow("masked", coadded);
	    cv::waitKey();
	}
}


std::vector<cv::Mat> extract_frames(const std::string &video, float const &superres) {

    std::vector<cv::Mat> frames;

    // ffmpeg API objects
    AVFormatContext* informat_ctx = NULL;
    AVFrame* frame = NULL;
    AVFrame* decframe = NULL;
    AVCodecContext* inav_ctx = NULL;
    AVCodec* in_codec = NULL;
    AVStream* instream = NULL;

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

    instream = informat_ctx->streams[video_stream];
    in_codec = avcodec_find_decoder(instream->codec->codec_id);
    if (in_codec == NULL) {
        std::cout << "Could not find codec: " << avcodec_get_name(instream->codec->codec_id) << std::endl;
        exit(1);
    }
    else std::cout << "Detected codec: " << avcodec_get_name(instream->codec->codec_id) << std::endl;
    std::cout << "File format:  " << informat_ctx->iformat->name << std::endl;
    std::cout << "Pixel format: " << av_get_pix_fmt_name(instream->codec->pix_fmt) << std::endl;
    inav_ctx = instream->codec;

    // Open the input codec
    avcodec_open2(inav_ctx, in_codec, NULL);

    // Allocate destination frame buffer
    std::vector<uint8_t> framebuf(avpicture_get_size(inav_ctx->pix_fmt, inav_ctx->width, inav_ctx->height));
    avpicture_fill(reinterpret_cast<AVPicture*>(frame), framebuf.data(), AV_PIX_FMT_BGR24, instream->codec->width, instream->codec->height);
                   
    SwsContext* swsctx = sws_getContext(inav_ctx->width, inav_ctx->height, instream->codec->pix_fmt, inav_ctx->width, 
                                        inav_ctx->height, AV_PIX_FMT_BGR24, SWS_BICUBIC, NULL, NULL, NULL);
    size_t nframes = instream->nb_frames;
    frames.reserve(nframes);    // Minimize memcpy
    
    size_t current(0);
    size_t previous(0);

    // Packet initialization
    AVPacket pkt;
    av_init_packet(&pkt);

    // Parsing loop
    while (ret == 0) {

        // Print progress
        print_percent(current++, previous, nframes);

        ret = av_read_frame(informat_ctx, &pkt);
        avcodec_decode_video2(instream->codec, decframe, &valid_frame, &pkt);

        // Ignore invalid frames
        if (!valid_frame) continue;

        // Frame extraction
        if (ret == 0) {                                              
            sws_scale(swsctx, decframe->data, decframe->linesize, 0, decframe->height, frame->data, frame->linesize);

            // Convert the decoded frame into a cv::Mat TODO: type deduction for different input pixelformats
            cv::Mat _frame(decframe->height, decframe->width, CV_8UC3, framebuf.data());
            cv::Mat resized;
            cv::resize(_frame, resized, cv::Size(decframe->width*superres, decframe->height*superres), 0, 0, cv::INTER_LANCZOS4);
            
            frames.push_back(resized);   // Have to use .clone() otherwise each element in [frames] will reference the same object
        }
    }
    print_percent(nframes-1, nframes);

    // Free memory and close streams
    av_frame_free(&decframe);
    av_frame_free(&frame);
    sws_freeContext(swsctx);
    avcodec_close(inav_ctx);
    avformat_close_input(&informat_ctx);

    return frames;
}

cv::Mat coadd(const std::vector<cv::Mat> &frames) {
    if (frames.empty()) return cv::Mat();

    // Create a 0 initialized image to use as accumulator
    cv::Mat m(frames[0].rows, frames[0].cols, CV_64FC3);
    m.setTo(cv::Scalar(0, 0, 0, 0));

    // Initialize output
    cv::Mat out(m.rows, m.cols, CV_64FC3);

    std::cout << "Accumulating..." << std::endl;

    const size_t nframes = frames.size();
    size_t current(0);
    size_t previous(0);

    cv::Mat temp(m.rows, m.cols, CV_64FC3);
    for (auto fr : frames) {
        fr.convertTo(temp, CV_64FC3);
        m += temp;
        print_percent(current++, previous, nframes);
    }
    print_percent(current-1, nframes);

    std::cout << "Dividing..." << std::flush;
    m.convertTo(out, CV_64FC3, 1.0 / nframes);
    std::cout << bright+green+"done"+res+"." << std::endl;

    return out;
}

void unsharpMask(cv::Mat &original, unsigned int scale, double const &sigma, double const &thresh) {

    // Convert original image to CV_64FC3
    cv::Mat _original(original.rows, original.cols, CV_64FC3);
    original.convertTo(_original, CV_64FC3);

    // Create Gaussian-blurred image with square kernel of size [scale, scale]
    if (scale % 2 == 0) scale += 1;     // Scale has to be odd
    cv::Mat blurred(original.rows, original.cols, CV_64FC3);
    cv::GaussianBlur(_original, blurred, cv::Size(scale, scale), sigma);

    cv::Mat masked(original.rows, original.cols, CV_64FC3);

    // Apply the unsharp masking
    masked = _original + (2 * (sigma / 100.0) * (_original - blurred));

    // Filter out negative values
    double* m = masked.ptr<double>(0, 0);
    for (size_t idx = 0; idx < masked.total(); idx++, m++) {
        if (*m < 0) *m = 0;
    }

    masked.convertTo(original, CV_8UC3);
}
