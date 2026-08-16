#pragma once

#include <opencv2/core.hpp>

#include <string>
#include <vector>

#if defined(STRIPE_DEFECT_DETECT_STATIC)
#define STRIPE_DEFECT_DETECT_API
#elif defined(_WIN32) || defined(__CYGWIN__)
#if defined(STRIPE_DEFECT_DETECT_EXPORTS)
#define STRIPE_DEFECT_DETECT_API __declspec(dllexport)
#else
#define STRIPE_DEFECT_DETECT_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) && __GNUC__ >= 4
#define STRIPE_DEFECT_DETECT_API __attribute__((visibility("default")))
#else
#define STRIPE_DEFECT_DETECT_API
#endif

namespace stripe_defect {

/// 傅里叶响应的缺陷极性。
enum class StripePolarity {
    Bright,  ///< 仅检测亮条纹。
    Dark,    ///< 仅检测暗条纹。
    Both,    ///< 同时检测亮条纹和暗条纹。
};

/// 水平条纹缺陷检测参数。
///
/// 所有长度和矩形均使用输入图像坐标。roi 为空时表示整幅图像。
/// maskedRois 为屏蔽区域：区域内的像素不参与行轮廓计算；如果某一行被
/// 完全屏蔽，则该行不会被选为条纹缺陷位置。
struct STRIPE_DEFECT_DETECT_API StripeDefectParams {
    /// 用于构建傅里叶行轮廓的处理区域；为空时使用整幅图像。
    cv::Rect roi{};

    /// roi 内用于搜索和评分的区域。使用较大的 roi 保留上下文，同时通过
    /// detectionRoi 限制峰值搜索范围，可减少傅里叶边界伪影；为空时使用 roi。
    cv::Rect detectionRoi{};

    /// 需要排除的屏蔽区域列表，坐标相对于输入图像；允许设置多个区域。
    std::vector<cv::Rect> maskedRois{};

    /// 高斯预滤波核的水平和垂直尺寸。偶数会自动增加为下一个奇数。
    /// 默认值与迁移前的傅里叶测试实现保持一致。
    int horizontalBlurSize = 15;
    int verticalBlurSize = 5;

    /// 水平方向分块数量。算法计算各分块的行均值，再取中位数生成行轮廓，
    /// 从而降低局部物体和部分屏蔽区域对检测结果的影响。
    int tileCount = 16;

    /// 傅里叶带通滤波保留的最小和最大条纹周期，单位为图像行数。
    double minimumPeriod = 30.0;
    double maximumPeriod = 120.0;

    /// 缺陷判定阈值；响应分数严格大于该值时判定为 NG。
    double threshold = 0.48;

    /// 要检测的条纹极性，默认为亮条纹。
    StripePolarity polarity = StripePolarity::Bright;

    /// 返回缺陷矩形的半高度，单位为图像行数。
    int defectHalfHeight = 12;
};

struct STRIPE_DEFECT_DETECT_API StripeDefectResult {
    /// 是否检测到条纹缺陷；true 表示 NG，false 表示 OK。
    bool isDefect = false;

    /// 傅里叶响应分数。
    double score = 0.0;

    /// 条纹中心在输入图像中的纵坐标；检测失败时为 -1。
    int centerY = -1;

    /// 实际参与傅里叶行轮廓计算的区域。
    cv::Rect inspectedRoi{};

    /// 实际用于峰值搜索和评分的区域。
    cv::Rect evaluatedRoi{};

    /// 根据检测中心和 defectHalfHeight 生成的缺陷矩形。
    cv::Rect defectRoi{};

    /// 调试和诊断数据：参与傅里叶处理的鲁棒行轮廓。
    /// 矩阵包含 inspectedRoi.height 行和一个 CV_32F 列，矩阵第 0 行
    /// 对应输入图像的 inspectedRoi.y。
    cv::Mat rowProfile;

    /// 调试和诊断数据：经过傅里叶带通滤波后的响应。
    /// 矩阵尺寸和坐标关系与 rowProfile 相同。
    cv::Mat fourierResponse;
};

/// 检测接口返回状态。
enum class StripeDefectStatus {
    Ok = 0,                    ///< 检测成功。
    EmptyImage = 1,            ///< 输入图像为空。
    UnsupportedImageType = 2, ///< 输入图像类型不受支持。
    InvalidRoi = 3,            ///< ROI 参数无效或不与图像相交。
    InvalidParameter = 4,      ///< 检测参数无效。
    NoUsablePixels = 5,        ///< ROI 内没有可用于检测的像素。
};

/// 使用一维傅里叶带通滤波检测水平条纹缺陷。
///
/// 支持 CV_8UC1 灰度图、CV_8UC3 BGR 图和 CV_8UC4 BGRA 图。
/// 本函数不会修改输入图像。检测失败时会重置 result；如果 errorMessage
/// 不为空，则写入便于阅读的错误说明。
///
/// @param image 输入图像。
/// @param params 检测参数。
/// @param result 检测结果。
/// @param errorMessage 可选的错误信息输出指针，允许为空。
/// @return 检测接口返回状态；Ok 表示调用成功。
STRIPE_DEFECT_DETECT_API StripeDefectStatus detectStripeDefect(
    const cv::Mat& image,
    const StripeDefectParams& params,
    StripeDefectResult& result,
    std::string* errorMessage = nullptr);

/// 获取检测状态对应的简短说明文本。
/// @param status 检测接口返回状态。
/// @return 状态说明字符串，字符串由库管理，调用方不得释放。
STRIPE_DEFECT_DETECT_API const char* stripeDefectStatusMessage(
    StripeDefectStatus status) noexcept;

}  // 命名空间 stripe_defect
