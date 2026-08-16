#include "stripe_defect_detect.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace stripe_defect {
namespace {

StripeDefectStatus fail(StripeDefectStatus status, const char* message,
                        StripeDefectResult& result,
                        std::string* errorMessage) {
    result = StripeDefectResult{};
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
    return status;
}

int positiveOdd(int value) {
    return value % 2 == 0 ? value + 1 : value;
}

cv::Mat toGray(const cv::Mat& image) {
    if (image.type() == CV_8UC1) {
        return image;
    }

    cv::Mat gray;
    if (image.type() == CV_8UC3) {
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    } else {
        cv::cvtColor(image, gray, cv::COLOR_BGRA2GRAY);
    }
    return gray;
}

cv::Mat maskedGaussianBlur(const cv::Mat& panel, const cv::Mat& validMask,
                           const cv::Size& kernelSize) {
    cv::Mat panelFloat;
    cv::Mat maskFloat;
    panel.convertTo(panelFloat, CV_32F);
    validMask.convertTo(maskFloat, CV_32F, 1.0 / 255.0);

    cv::Mat weighted;
    cv::multiply(panelFloat, maskFloat, weighted);
    cv::GaussianBlur(weighted, weighted, kernelSize, 0.0, 0.0,
                     cv::BORDER_REFLECT101);
    cv::GaussianBlur(maskFloat, maskFloat, kernelSize, 0.0, 0.0,
                     cv::BORDER_REFLECT101);

    cv::Mat blurred = cv::Mat::zeros(panel.size(), CV_32F);
    cv::divide(weighted, maskFloat, blurred, 1.0, CV_32F);
    blurred.setTo(0.0F, maskFloat <= std::numeric_limits<float>::epsilon());
    return blurred;
}

float median(std::vector<float>& values) {
    const std::size_t middle = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + middle, values.end());
    const float upper = values[middle];
    if (values.size() % 2 != 0) {
        return upper;
    }

    std::nth_element(values.begin(), values.begin() + middle - 1,
                     values.begin() + middle);
    return 0.5F * (values[middle - 1] + upper);
}

void fillMissingRows(cv::Mat& profile, const std::vector<unsigned char>& usableRows) {
    int firstUsable = -1;
    for (int row = 0; row < profile.rows; ++row) {
        if (usableRows[static_cast<std::size_t>(row)] != 0) {
            firstUsable = row;
            break;
        }
    }

    for (int row = 0; row < firstUsable; ++row) {
        profile.at<float>(row, 0) = profile.at<float>(firstUsable, 0);
    }

    int previous = firstUsable;
    for (int row = firstUsable + 1; row < profile.rows; ++row) {
        if (usableRows[static_cast<std::size_t>(row)] == 0) {
            continue;
        }

        const int gap = row - previous;
        if (gap > 1) {
            const float begin = profile.at<float>(previous, 0);
            const float end = profile.at<float>(row, 0);
            for (int offset = 1; offset < gap; ++offset) {
                const float weight = static_cast<float>(offset) / gap;
                profile.at<float>(previous + offset, 0) =
                    begin + weight * (end - begin);
            }
        }
        previous = row;
    }

    for (int row = previous + 1; row < profile.rows; ++row) {
        profile.at<float>(row, 0) = profile.at<float>(previous, 0);
    }
}

cv::Mat makeRobustRowProfile(const cv::Mat& panel, const cv::Mat& validMask,
                             int tileCount,
                             std::vector<unsigned char>& usableRows) {
    cv::Mat panelFloat;
    panel.convertTo(panelFloat, CV_32F);

    cv::Mat profile(panel.rows, 1, CV_32F,
                    cv::Scalar(std::numeric_limits<float>::quiet_NaN()));
    usableRows.assign(static_cast<std::size_t>(panel.rows), 0);

    const int actualTileCount = std::min(tileCount, panel.cols);
    std::vector<cv::Mat> tileSums;
    std::vector<cv::Mat> tileCounts;
    tileSums.reserve(static_cast<std::size_t>(actualTileCount));
    tileCounts.reserve(static_cast<std::size_t>(actualTileCount));

    for (int tile = 0; tile < actualTileCount; ++tile) {
        const int left = tile * panel.cols / actualTileCount;
        const int right = (tile + 1) * panel.cols / actualTileCount;
        const cv::Range columns(left, right);

        cv::Mat tileMaskFloat;
        validMask.colRange(columns).convertTo(tileMaskFloat, CV_32F, 1.0 / 255.0);
        cv::Mat weighted;
        cv::multiply(panelFloat.colRange(columns), tileMaskFloat, weighted);

        cv::Mat sums;
        cv::Mat counts;
        cv::reduce(weighted, sums, 1, cv::REDUCE_SUM, CV_32F);
        cv::reduce(tileMaskFloat, counts, 1, cv::REDUCE_SUM, CV_32F);
        tileSums.push_back(std::move(sums));
        tileCounts.push_back(std::move(counts));
    }

    std::vector<float> values;
    values.reserve(static_cast<std::size_t>(actualTileCount));
    for (int row = 0; row < panel.rows; ++row) {
        values.clear();
        for (int tile = 0; tile < actualTileCount; ++tile) {
            const float count = tileCounts[static_cast<std::size_t>(tile)]
                                    .at<float>(row, 0);
            if (count > 0.0F) {
                const float sum = tileSums[static_cast<std::size_t>(tile)]
                                      .at<float>(row, 0);
                values.push_back(sum / count);
            }
        }

        if (!values.empty()) {
            profile.at<float>(row, 0) = median(values);
            usableRows[static_cast<std::size_t>(row)] = 1;
        }
    }

    fillMissingRows(profile, usableRows);
    return profile;
}

cv::Mat fourierBandPass(const cv::Mat& profile, double minimumPeriod,
                        double maximumPeriod) {
    cv::Mat centered = profile - cv::mean(profile)[0];
    cv::Mat spectrum;
    cv::dft(centered, spectrum, cv::DFT_COMPLEX_OUTPUT);

    for (int index = 0; index < spectrum.rows; ++index) {
        const int mirroredIndex = std::min(index, spectrum.rows - index);
        const double frequency =
            static_cast<double>(mirroredIndex) / spectrum.rows;
        const bool keep = frequency >= 1.0 / maximumPeriod &&
                          frequency <= 1.0 / minimumPeriod;
        if (!keep) {
            spectrum.at<cv::Vec2f>(index, 0) = cv::Vec2f(0.0F, 0.0F);
        }
    }

    cv::Mat response;
    cv::dft(spectrum, response,
            cv::DFT_INVERSE | cv::DFT_REAL_OUTPUT | cv::DFT_SCALE);
    return response;
}

double responseScore(float response, StripePolarity polarity) {
    switch (polarity) {
        case StripePolarity::Bright:
            return response;
        case StripePolarity::Dark:
            return -static_cast<double>(response);
        case StripePolarity::Both:
            return std::abs(static_cast<double>(response));
    }
    return -std::numeric_limits<double>::infinity();
}

}  // namespace

StripeDefectStatus detectStripeDefect(const cv::Mat& image,
                                      const StripeDefectParams& params,
                                      StripeDefectResult& result,
                                      std::string* errorMessage) {
    result = StripeDefectResult{};
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    if (image.empty()) {
        return fail(StripeDefectStatus::EmptyImage, "input image is empty",
                    result, errorMessage);
    }
    if (image.type() != CV_8UC1 && image.type() != CV_8UC3 &&
        image.type() != CV_8UC4) {
        return fail(StripeDefectStatus::UnsupportedImageType,
                    "expected an 8-bit gray, BGR, or BGRA image", result,
                    errorMessage);
    }
    if (params.horizontalBlurSize <= 0 || params.verticalBlurSize <= 0 ||
        params.tileCount <= 0 || params.minimumPeriod < 2.0 ||
        params.maximumPeriod <= params.minimumPeriod ||
        !std::isfinite(params.minimumPeriod) ||
        !std::isfinite(params.maximumPeriod) ||
        !std::isfinite(params.threshold) || params.threshold < 0.0 ||
        params.defectHalfHeight < 0) {
        return fail(StripeDefectStatus::InvalidParameter,
                    "invalid blur, tile, Fourier period, threshold, or band parameter",
                    result, errorMessage);
    }

    const cv::Rect imageBounds(0, 0, image.cols, image.rows);
    const bool useFullImage = params.roi.width == 0 && params.roi.height == 0;
    if (!useFullImage && (params.roi.width <= 0 || params.roi.height <= 0)) {
        return fail(StripeDefectStatus::InvalidRoi,
                    "roi dimensions must both be positive or both be zero",
                    result, errorMessage);
    }
    const cv::Rect requestedRoi = useFullImage ? imageBounds : params.roi;
    const cv::Rect roi = requestedRoi & imageBounds;
    if (roi.width <= 0 || roi.height < 3) {
        return fail(StripeDefectStatus::InvalidRoi,
                    "roi must intersect the image and contain at least three rows",
                    result, errorMessage);
    }

    const bool useWholeRoi =
        params.detectionRoi.width == 0 && params.detectionRoi.height == 0;
    if (!useWholeRoi &&
        (params.detectionRoi.width <= 0 || params.detectionRoi.height <= 0)) {
        return fail(StripeDefectStatus::InvalidRoi,
                    "detectionRoi dimensions must both be positive or both be zero",
                    result, errorMessage);
    }
    const cv::Rect detectionRoi =
        (useWholeRoi ? roi : params.detectionRoi) & roi;
    if (detectionRoi.area() <= 0) {
        return fail(StripeDefectStatus::InvalidRoi,
                    "detectionRoi must intersect roi", result, errorMessage);
    }

    const cv::Mat gray = toGray(image);
    const cv::Mat unfilteredPanel = gray(roi);
    const cv::Size blurKernel(positiveOdd(params.horizontalBlurSize),
                              positiveOdd(params.verticalBlurSize));
    cv::Mat validMask(unfilteredPanel.size(), CV_8U, cv::Scalar(255));
    for (const cv::Rect& maskedRoi : params.maskedRois) {
        if (maskedRoi.width == 0 && maskedRoi.height == 0) {
            continue;
        }
        if (maskedRoi.width <= 0 || maskedRoi.height <= 0) {
            return fail(StripeDefectStatus::InvalidRoi,
                        "masked ROI dimensions must be positive", result,
                        errorMessage);
        }
        const cv::Rect clipped = maskedRoi & roi;
        if (clipped.area() <= 0) {
            continue;
        }
        const cv::Rect local(clipped.x - roi.x, clipped.y - roi.y,
                             clipped.width, clipped.height);
        validMask(local).setTo(0);
    }
    const int validPixelCount = cv::countNonZero(validMask);
    if (validPixelCount == 0) {
        return fail(StripeDefectStatus::NoUsablePixels,
                    "masked rois exclude every pixel in roi", result,
                    errorMessage);
    }

    cv::Mat panel;
    if (static_cast<std::size_t>(validPixelCount) == validMask.total()) {
        cv::GaussianBlur(unfilteredPanel, panel, blurKernel, 0.0, 0.0,
                         cv::BORDER_REFLECT101);
    } else {
        panel = maskedGaussianBlur(unfilteredPanel, validMask, blurKernel);
    }

    std::vector<unsigned char> usableRows;
    result.rowProfile = makeRobustRowProfile(
        panel, validMask, params.tileCount, usableRows);
    result.fourierResponse = fourierBandPass(
        result.rowProfile, params.minimumPeriod, params.maximumPeriod);
    result.inspectedRoi = roi;
    result.evaluatedRoi = detectionRoi;

    double bestScore = -std::numeric_limits<double>::infinity();
    int bestRow = -1;
    const int firstDetectionRow = detectionRoi.y - roi.y;
    const int lastDetectionRow = firstDetectionRow + detectionRoi.height;
    for (int row = firstDetectionRow; row < lastDetectionRow; ++row) {
        if (usableRows[static_cast<std::size_t>(row)] == 0) {
            continue;
        }
        const double score = responseScore(
            result.fourierResponse.at<float>(row, 0), params.polarity);
        if (score > bestScore) {
            bestScore = score;
            bestRow = row;
        }
    }

    if (bestRow < 0) {
        return fail(StripeDefectStatus::NoUsablePixels,
                    "masked rois exclude every row in detectionRoi", result,
                    errorMessage);
    }

    result.score = std::max(0.0, bestScore);
    result.isDefect = result.score > params.threshold;
    result.centerY = roi.y + bestRow;
    const int top = std::max(detectionRoi.y,
                             result.centerY - params.defectHalfHeight);
    const int bottom = std::min(detectionRoi.y + detectionRoi.height,
                                result.centerY + params.defectHalfHeight + 1);
    result.defectRoi = cv::Rect(detectionRoi.x, top, detectionRoi.width,
                                bottom - top);
    return StripeDefectStatus::Ok;
}

const char* stripeDefectStatusMessage(StripeDefectStatus status) noexcept {
    switch (status) {
        case StripeDefectStatus::Ok:
            return "ok";
        case StripeDefectStatus::EmptyImage:
            return "empty image";
        case StripeDefectStatus::UnsupportedImageType:
            return "unsupported image type";
        case StripeDefectStatus::InvalidRoi:
            return "invalid roi";
        case StripeDefectStatus::InvalidParameter:
            return "invalid parameter";
        case StripeDefectStatus::NoUsablePixels:
            return "no usable pixels";
    }
    return "unknown status";
}

}  // namespace stripe_defect
