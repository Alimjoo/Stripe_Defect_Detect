#include "stripe_defect_detect.h"
#include "test_sdd.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string lowerExtension(const fs::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) {
                       return static_cast<char>(std::tolower(value));
                   });
    return extension;
}

bool isImage(const fs::path& path) {
    const std::string extension = lowerExtension(path);
    return extension == ".jpg" || extension == ".jpeg" ||
           extension == ".png" || extension == ".bmp" ||
           extension == ".tif" || extension == ".tiff";
}

std::vector<fs::path> collectImages(const fs::path& input) {
    if (fs::is_regular_file(input)) {
        if (!isImage(input)) {
            throw std::runtime_error("unsupported image extension: " +
                                     input.string());
        }
        return {input};
    }
    if (!fs::is_directory(input)) {
        throw std::runtime_error("input path does not exist: " + input.string());
    }

    std::vector<fs::path> images;
    for (const fs::directory_entry& entry : fs::directory_iterator(input)) {
        if (entry.is_regular_file() && isImage(entry.path())) {
            images.push_back(entry.path());
        }
    }
    std::sort(images.begin(), images.end());
    return images;
}

cv::Rect parseRect(const std::string& value) {
    std::vector<int> fields;
    std::size_t begin = 0;
    while (begin <= value.size()) {
        const std::size_t end = value.find(',', begin);
        const std::string field = value.substr(begin, end - begin);
        std::size_t consumed = 0;
        const int parsed = std::stoi(field, &consumed);
        if (consumed != field.size()) {
            throw std::runtime_error("invalid rectangle: " + value);
        }
        fields.push_back(parsed);
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1;
    }
    if (fields.size() != 4 || fields[2] <= 0 || fields[3] <= 0) {
        throw std::runtime_error(
            "rectangle must be x,y,width,height with positive size: " + value);
    }
    return cv::Rect(fields[0], fields[1], fields[2], fields[3]);
}

cv::Rect makeLegacyProcessingRoi(const cv::Size& size) {
    const cv::Rect bounds(0, 0, size.width, size.height);
    return cv::Rect(cvRound(size.width * (120.0 / 1368.0)),
                    cvRound(size.height * (720.0 / 2048.0)),
                    cvRound(size.width * (1090.0 / 1368.0)),
                    cvRound(size.height * (620.0 / 2048.0))) &
           bounds;
}

cv::Rect makeLegacyDetectionRoi(const cv::Size& size,
                                const cv::Rect& processingRoi) {
    const int top = cvRound(size.height * 0.560);
    const int bottom = cvRound(size.height * 0.630);
    return cv::Rect(processingRoi.x, top, processingRoi.width, bottom - top) &
           processingRoi;
}

int scaledOdd(int referenceValue, double scale) {
    int value = std::max(1, cvRound(referenceValue * scale));
    return value % 2 == 0 ? value + 1 : value;
}

void saveResult(const cv::Mat& gray,
                const stripe_defect::StripeDefectParams& params,
                const stripe_defect::StripeDefectResult& result,
                const fs::path& imagePath) {
    cv::Mat annotated;
    cv::cvtColor(gray, annotated, cv::COLOR_GRAY2BGR);
    const int thickness = std::max(2, annotated.cols / 700);
    cv::rectangle(annotated, result.inspectedRoi, cv::Scalar(255, 180, 0),
                  thickness);
    cv::rectangle(annotated, result.evaluatedRoi, cv::Scalar(255, 0, 255),
                  thickness);
    for (const cv::Rect& maskedRoi : params.maskedRois) {
        cv::rectangle(annotated, maskedRoi, cv::Scalar(180, 180, 180),
                      thickness);
    }
    if (result.isDefect) {
        cv::rectangle(annotated, result.defectRoi, cv::Scalar(0, 0, 255),
                      thickness * 2);
    }

    const std::string label =
        std::string(result.isDefect ? "NG" : "OK") + "  score=" +
        cv::format("%.3f", result.score) + "  y=" +
        std::to_string(result.centerY);
    const double fontScale = std::max(0.65, annotated.cols / 1600.0);
    cv::putText(annotated, label,
                cv::Point(result.inspectedRoi.x + 5,
                          std::max(30, result.inspectedRoi.y - 12)),
                cv::FONT_HERSHEY_SIMPLEX, fontScale,
                result.isDefect ? cv::Scalar(0, 0, 255)
                                : cv::Scalar(0, 200, 0),
                thickness, cv::LINE_AA);
    if (!cv::imwrite(imagePath.string(), annotated)) {
        throw std::runtime_error("failed to save: " + imagePath.string());
    }
}

void saveProfile(const stripe_defect::StripeDefectResult& result,
                 const fs::path& csvPath) {
    std::ofstream output(csvPath);
    if (!output) {
        throw std::runtime_error("failed to create: " + csvPath.string());
    }
    output << "roi_y,image_y,row_profile,fourier_response,is_peak\n";
    output << std::fixed << std::setprecision(6);
    for (int row = 0; row < result.rowProfile.rows; ++row) {
        const int imageY = result.inspectedRoi.y + row;
        output << row << ',' << imageY << ','
               << result.rowProfile.at<float>(row, 0) << ','
               << result.fourierResponse.at<float>(row, 0) << ','
               << (imageY == result.centerY ? 1 : 0) << '\n';
    }
}

void printUsage(const char* program) {
    std::cerr
        << "Usage: " << program
        << " <image-or-directory> [output-directory] [options]\n"
        << "Options:\n"
        << "  --self-test          Run API tests and exit\n"
        << "  --roi x,y,w,h       ROI used to construct the row profile\n"
        << "  --detection-roi x,y,w,h  Limit peak scoring inside --roi\n"
        << "  --mask x,y,w,h      Exclude an ROI; may be specified repeatedly\n"
        << "  --full-image-roi     Use the full image instead of legacy camera ROIs\n"
        << "  --threshold value   Fourier NG threshold (default: 0.48)\n"
        << "  --min-period value  Minimum retained wavelength (default: 30)\n"
        << "  --max-period value  Maximum retained wavelength (default: 120)\n"
        << "  --full-resolution   Disable OpenCV's 1/8 reduced JPEG decode\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--self-test") {
        return testStripeDefectDetect();
    }
    if (argc < 2) {
        printUsage(argv[0]);
        return 2;
    }

    try {
        const fs::path input(argv[1]);
        int argument = 2;
        fs::path outputDirectory("results");
        if (argument < argc && std::string(argv[argument]).rfind("--", 0) != 0) {
            outputDirectory = fs::path(argv[argument++]);
        }

        stripe_defect::StripeDefectParams params;
        bool fullResolution = false;
        bool customRoi = false;
        bool customDetectionRoi = false;
        bool customMinimumPeriod = false;
        bool customMaximumPeriod = false;
        while (argument < argc) {
            const std::string option(argv[argument++]);
            if (option == "--full-resolution") {
                fullResolution = true;
            } else if (option == "--full-image-roi") {
                customRoi = true;
                customDetectionRoi = true;
                params.roi = cv::Rect();
                params.detectionRoi = cv::Rect();
            } else if (argument >= argc) {
                throw std::runtime_error("missing value after " + option);
            } else if (option == "--roi") {
                params.roi = parseRect(argv[argument++]);
                customRoi = true;
            } else if (option == "--detection-roi") {
                params.detectionRoi = parseRect(argv[argument++]);
                customDetectionRoi = true;
            } else if (option == "--mask") {
                params.maskedRois.push_back(parseRect(argv[argument++]));
            } else if (option == "--threshold") {
                params.threshold = std::stod(argv[argument++]);
            } else if (option == "--min-period") {
                params.minimumPeriod = std::stod(argv[argument++]);
                customMinimumPeriod = true;
            } else if (option == "--max-period") {
                params.maximumPeriod = std::stod(argv[argument++]);
                customMaximumPeriod = true;
            } else {
                throw std::runtime_error("unknown option: " + option);
            }
        }

        const std::vector<fs::path> images = collectImages(input);
        if (images.empty()) {
            throw std::runtime_error("no supported images found in: " +
                                     input.string());
        }
        fs::create_directories(outputDirectory);

        int failures = 0;
        for (const fs::path& path : images) {
            const int readMode = fullResolution ? cv::IMREAD_GRAYSCALE
                                                : cv::IMREAD_REDUCED_GRAYSCALE_8;
            const cv::Mat image = cv::imread(path.string(), readMode);
            if (image.empty()) {
                std::cerr << "Could not read: " << path << '\n';
                ++failures;
                continue;
            }

            stripe_defect::StripeDefectParams effectiveParams = params;
            if (!customRoi) {
                effectiveParams.roi = makeLegacyProcessingRoi(image.size());
            }
            if (!customDetectionRoi) {
                effectiveParams.detectionRoi = customRoi
                                                   ? cv::Rect()
                                                   : makeLegacyDetectionRoi(
                                                         image.size(),
                                                         effectiveParams.roi);
            }
            const double blurScale = image.rows / 2048.0;
            effectiveParams.horizontalBlurSize = scaledOdd(15, blurScale);
            effectiveParams.verticalBlurSize = scaledOdd(5, blurScale);
            effectiveParams.defectHalfHeight = scaledOdd(12, blurScale);
            const double periodScale = image.rows / 2655.0;
            if (!customMinimumPeriod) {
                effectiveParams.minimumPeriod = 30.0 * periodScale;
            }
            if (!customMaximumPeriod) {
                effectiveParams.maximumPeriod = 120.0 * periodScale;
            }

            stripe_defect::StripeDefectResult result;
            std::string error;
            const stripe_defect::StripeDefectStatus status =
                stripe_defect::detectStripeDefect(image, effectiveParams,
                                                  result, &error);
            if (status != stripe_defect::StripeDefectStatus::Ok) {
                std::cerr << "Could not process " << path.filename().string()
                          << ": " << error << '\n';
                ++failures;
                continue;
            }

            const std::string stem = path.stem().string();
            const fs::path resultPath = outputDirectory / (stem + "_result.jpg");
            const fs::path csvPath = outputDirectory / (stem + "_fourier.csv");
            saveResult(image, effectiveParams, result, resultPath);
            saveProfile(result, csvPath);
            std::cout << (result.isDefect ? "NG" : "OK") << "  score="
                      << std::fixed << std::setprecision(3) << result.score
                      << "  y=" << result.centerY << "  "
                      << path.filename().string() << '\n';
        }
        return failures == 0 ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        printUsage(argv[0]);
        return 2;
    }
}
