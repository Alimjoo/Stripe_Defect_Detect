# Stripe Defect Detect

C++17/OpenCV library for horizontal stripe-defect detection using a robust
one-dimensional row profile and a Fourier band-pass filter. The implementation
was migrated from `test_stripe_defect` into a reusable library plus a small CLI.

## Public API

Include `stripe_defect_detect.h` and link the `Stripe_Defect_Detect` CMake
target. Every tuning value is part of `StripeDefectParams`; the defaults are
declared in the public header.

```cpp
#include <stripe_defect_detect.h>

stripe_defect::StripeDefectParams params;
params.roi = cv::Rect(150, 933, 1362, 804);          // Fourier profile area
params.detectionRoi = cv::Rect(150, 1487, 1362, 186); // peak-search area
params.maskedRois = {
    cv::Rect(300, 1500, 120, 80), // excluded / 屏蔽 ROI
};
params.minimumPeriod = 30.0;
params.maximumPeriod = 120.0;
params.threshold = 0.48;

stripe_defect::StripeDefectResult result;
std::string error;
const auto status = stripe_defect::detectStripeDefect(
    image, params, result, &error);

if (status == stripe_defect::StripeDefectStatus::Ok && result.isDefect) {
    // result.score, result.centerY, and result.defectRoi are available here.
}
```

All ROI coordinates refer to the input image. `roi` supplies the wider context
used to construct the Fourier profile. `detectionRoi` optionally limits where a
peak may be selected and defaults to `roi`. Every rectangle in `maskedRois` is
excluded from the row statistics; multiple exclusion areas are supported.

Accepted images are 8-bit grayscale, BGR, or BGRA `cv::Mat` objects. The API
does not modify the input image.

## Build and test

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
# The same test function can also be called through the only main executable:
./build/stripe_detector --self-test
```

The command-line test runner keeps the normalized camera ROIs and reduced JPEG
decode used by the original project:

```sh
./build/stripe_detector <image-or-directory> [output-directory]
```

Run `./build/stripe_detector` without arguments to see ROI, mask, threshold,
period-band, and full-resolution options.

## Windows x64 CI package

The `Build Windows x64 package` GitHub Actions workflow builds the shared
library with MSVC for both Debug and Release. It uses OpenCV 4.12.0 while
building and testing, but does not put OpenCV files in the published ZIP. A
tag matching `v*` also creates or updates the corresponding GitHub Release and
attaches the compiled ZIP automatically.

```text
Stripe_Defect_Detect-1.0.0-windows-x64/
├── stripe_defect_detect.h
├── Stripe_Defect_Detect.dll
├── Stripe_Defect_Detect.lib
├── Stripe_Defect_Detectd.dll
└── Stripe_Defect_Detectd.lib
```

Files ending in `d` are the Debug build; files without `d` are the Release
build.

Applications using this package must provide their own compatible OpenCV 4.12
headers, import libraries, and runtime DLLs. In particular, the OpenCV runtime
DLL directory must be available in `PATH` when the application starts.
