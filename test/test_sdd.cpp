#include "test_sdd.h"

#include "stripe_defect_detect.h"

#include <opencv2/core.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

cv::Mat makeBandImage(int centerY, int bandLeft, int bandRight) {
    cv::Mat image(256, 320, CV_8UC1, cv::Scalar(100));
    for (int row = 0; row < image.rows; ++row) {
        const double distance = (row - centerY) / 7.0;
        const int value = cvRound(100.0 + 24.0 * std::exp(-0.5 * distance * distance));
        for (int column = std::max(0, bandLeft);
             column < std::min(image.cols, bandRight); ++column) {
            image.at<unsigned char>(row, column) =
                static_cast<unsigned char>(value);
        }
    }
    return image;
}

void testDetectsBrightStripeAndUsesImageCoordinates() {
    const cv::Mat image = makeBandImage(132, 0, 320);
    stripe_defect::StripeDefectParams params;
    params.roi = cv::Rect(30, 24, 250, 208);
    params.threshold = 1.0;

    stripe_defect::StripeDefectResult result;
    std::string error;
    const auto status =
        stripe_defect::detectStripeDefect(image, params, result, &error);

    expect(status == stripe_defect::StripeDefectStatus::Ok,
           "bright stripe returns Ok: " + error);
    expect(result.isDefect, "bright stripe is classified as a defect");
    expect(std::abs(result.centerY - 132) <= 3,
           "centerY is returned in full-image coordinates");
    expect(result.inspectedRoi == params.roi, "result reports the clipped ROI");
    expect(result.evaluatedRoi == params.roi,
           "an empty detection ROI defaults to the processing ROI");
    expect(result.defectRoi.contains(cv::Point(100, result.centerY)),
           "defect ROI contains the detected center");
    expect(result.rowProfile.rows == params.roi.height &&
               result.fourierResponse.rows == params.roi.height,
           "diagnostic profiles match ROI height");
}

void testDetectionRoiLimitsPeakSelection() {
    cv::Mat image = makeBandImage(75, 0, 320);
    const cv::Mat lowerBand = makeBandImage(180, 0, 320);
    cv::max(image, lowerBand, image);

    stripe_defect::StripeDefectParams params;
    params.roi = cv::Rect(20, 20, 280, 215);
    params.detectionRoi = cv::Rect(20, 145, 280, 75);
    params.threshold = 1.0;

    stripe_defect::StripeDefectResult result;
    const auto status = stripe_defect::detectStripeDefect(image, params, result);
    expect(status == stripe_defect::StripeDefectStatus::Ok,
           "detection ROI request succeeds");
    expect(std::abs(result.centerY - 180) <= 3,
           "peak selection is limited to detectionRoi");
    expect(result.evaluatedRoi == params.detectionRoi,
           "result reports the evaluation ROI");
}

void testMaskedRoiRemovesItsPixelsFromProfile() {
    const cv::Mat image = makeBandImage(128, 0, 240);
    stripe_defect::StripeDefectParams params;
    params.threshold = 1.0;

    stripe_defect::StripeDefectResult unmasked;
    auto status = stripe_defect::detectStripeDefect(image, params, unmasked);
    expect(status == stripe_defect::StripeDefectStatus::Ok && unmasked.isDefect,
           "localized stripe is detected before masking");

    params.maskedRois.emplace_back(0, 0, 240, image.rows);
    stripe_defect::StripeDefectResult masked;
    status = stripe_defect::detectStripeDefect(image, params, masked);
    expect(status == stripe_defect::StripeDefectStatus::Ok,
           "partially masked image still has usable pixels");
    expect(!masked.isDefect, "masking the stripe suppresses its detection");
    expect(masked.score < unmasked.score * 0.1,
           "masked ROI does not leak materially into the row profile");
}

void testValidationAndColorInput() {
    stripe_defect::StripeDefectParams params;
    stripe_defect::StripeDefectResult result;
    expect(stripe_defect::detectStripeDefect(cv::Mat(), params, result) ==
               stripe_defect::StripeDefectStatus::EmptyImage,
           "empty image has a stable error status");

    cv::Mat gray = makeBandImage(120, 0, 320);
    cv::Mat bgr;
    cv::merge(std::vector<cv::Mat>{gray, gray, gray}, bgr);
    params.threshold = 1.0;
    expect(stripe_defect::detectStripeDefect(bgr, params, result) ==
               stripe_defect::StripeDefectStatus::Ok &&
               result.isDefect,
           "BGR input is accepted");

    params.maskedRois = {cv::Rect(0, 0, gray.cols, gray.rows)};
    expect(stripe_defect::detectStripeDefect(gray, params, result) ==
               stripe_defect::StripeDefectStatus::NoUsablePixels,
           "fully excluded ROI is rejected");
}

}  // namespace

int testStripeDefectDetect() {
    failures = 0;
    testDetectsBrightStripeAndUsesImageCoordinates();
    testDetectionRoiLimitsPeakSelection();
    testMaskedRoiRemovesItsPixelsFromProfile();
    testValidationAndColorInput();
    if (failures != 0) {
        std::cerr << failures << " test assertion(s) failed\n";
        return 1;
    }
    std::cout << "All stripe defect detector tests passed\n";
    return 0;
}
