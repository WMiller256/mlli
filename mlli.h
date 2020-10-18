/* mlli.c++
 *
 * William Miller
 * Oct 14 2020
 *
 * Machine-Learned Lucky Imaging for enhancing planetary imaging.
 *
 */

#pragma once

#include <experimental/filesystem>
#include <iomanip>
#include <iostream>
#include <vector>

// ffmpeg
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/timestamp.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

// Opencv
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/xfeatures2d.hpp>
#include <opencv2/features2d.hpp> 

// Boost
#include <boost/program_options.hpp>

// mlli
#include "colors.h"
#include "iocustom.h"

namespace po = boost::program_options;
namespace fs = std::experimental::filesystem;

std::vector<cv::Mat> extract_frames(const std::string &video, float const &superres);

cv::Mat coadd(const std::vector<cv::Mat> &frames);
cv::Mat unsharpMask(cv::Mat original, double const &scale, double const &sigma, double const &thresh=0);
