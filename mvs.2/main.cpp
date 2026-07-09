#include "CameraApi.h"        // 迈德威视SDK头文件
#include<opencv2/opencv.hpp>
#include <stdio.h>            // printf打印

using namespace cv;

int main()
{
    int                     iCameraCounts = 1;
    /*记录电脑上识别到的工业相机总数量*/
    int                     iStatus = -1;
    /*SDK 所有相机接口的返回状态码*/
    tSdkCameraDevInfo       tCameraEnumList;
    /*相机设备信息结构体
    存储内容：单台相机完整硬件信息
包含：相机序列号、设备型号、USB / 网口类型、IP 地址、设备名称等
作用：
CameraEnumerateDevice枚举相机时，把第一台相机信息填充到这个结构体；
再传入CameraInit打开对应相机设备。*/
    int                     hCamera;
    /*含义：相机唯一操作标识（相机句柄）
作用：
一台相机对应一个数字句柄；
后续所有操作（取图、设置参数、关闭相机）都要传入 hCamera，SDK 靠它区分操作哪台相机。
如果有两个相机的话就可以分别用数字代表*/
    tSdkCameraCapbility     tCapability;
    /*相机硬件能力 / 性能结构体
    存储内容：相机全部硬件规格上限
包含：最大支持分辨率、最小分辨率、传感器是否黑白、曝光范围、增益范围、支持的图像格式等
作用：
CameraGetCapability读取后，我们从中取出最大宽高，用来提前分配足够大的图像缓存；
判断bMonoSensor区分黑白 / 彩色相机。*/
    tSdkFrameHead           sFrameInfo;
    /*帧头信息结构体
    存储内容：当前这一帧图像的实时参数
包含：当前帧宽度 iWidth、高度 iHeight、图像格式 uiMediaType、采集时间戳、曝光值等
作用：
取到图像后，从这里获取实时宽高、图像类型，用来构造 OpenCV 的 Mat 图像。*/
    BYTE*                   pbyBuffer;
    /*原始裸图像数据指针
    作用：
CameraGetImageBuffer获取画面时，会把相机输出的原始拜耳 Bayer 裸图地址存入 pbyBuffer；
原始数据不能直接显示，必须传入CameraImageProcess转成 BGR / 灰度图才能给 OpenCV 使用；
使用完必须搭配CameraReleaseImageBuffer释放该缓存，否则内存泄漏、相机卡死。*/
    int                     iDisplayFrames = 10000;
    int                     channel = 3;

    // 视频相关变量
    VideoWriter app;
    bool isRecordStart = false;
    double fps = 20.0;
    int codec = VideoWriter::fourcc('M','J','P','G');
//1.SDK全局初始化==>开启 SDK 内部调试日志；程序最开头必须调用，加载相机底层驱动。
    CameraSdkInit(1);

    // 2.枚举相机
    iStatus = CameraEnumerateDevice(&tCameraEnumList, &iCameraCounts);
    /* 迈德威视 SDK 提供的相机枚举函数：扫描电脑上所有通过 USB / 网口连接的工业相机，读取相机硬件信息并统计相机数量。
     &tCameraEnumList

取相机设备信息结构体的地址
如果只连接 1 台相机，SDK 会把这台相机的序列号、型号、接口等信息存入 tCameraEnumList；多相机需要数组存储。
 &iCameraCounts

取整型变量地址
函数执行完毕后，这个变量会被赋值为当前电脑识别到的相机总台数。
返回值：int 状态码，0(CAMERA_STATUS_SUCCESS)：枚举操作成功，正常扫描完成；
非 0 数字：枚举失败（USB 端口异常、SDK 驱动缺失、权限不足等*/
    printf("state = %d\n", iStatus);
    printf("count = %d\n", iCameraCounts);
    if (iCameraCounts == 0) {
        printf("未检测到相机设备\n");
        return -1;
    }

    // 3.初始化相机
    iStatus = CameraInit(&tCameraEnumList, -1, -1, &hCamera);
    /*迈德威视 SDK 相机初始化函数，作用：打开指定工业相机设备，建立程序和相机的通信通道，生成相机句柄。
    &tCameraEnumList
传入枚举阶段获取到的相机设备信息结构体地址，SDK 根据这个结构体定位要打开哪一台相机（设备序列号、接口信息都存在这里）。
第一个 -1：相机通道号
多通道复合相机才需要填对应通道编号，普通单目工业相机统一传 -1，使用默认主通道。
第二个 -1：分辨率索引
相机内部预设多套分辨率档位，填数字选择对应档位；传 -1 代表直接使用相机出厂默认分辨率。
&hCamera
相机句柄变量的地址，属于输出参数。
初始化成功后，SDK 会给 hCamera 赋值一个唯一整数句柄，后续所有操作相机的函数都必须携带这个句柄；
初始化失败时，hCamera 值无效，不能使用。
*/
    printf("init state = %d\n", iStatus);
    if (iStatus != CAMERA_STATUS_SUCCESS) {
        printf("相机初始化失败\n");
        return -1;
    }
//4. 获取相机硬件参数
/*读取相机最大宽高、传感器类型，用于创建足够大的 Mat 缓存rgbBu
SDK 接口：读取当前已打开相机的全部硬件能力、规格参数，存到 tCapability 结构体中。*/
    CameraGetCapability(hCamera, &tCapability);

    // 用OpenCV Mat替代malloc分配缓存，自动管理内存，无需<cstdlib>
    int maxW = tCapability.sResolutionRange.iWidthMax;// 相机支持的最大宽度
    int maxH = tCapability.sResolutionRange.iHeightMax;// 相机支持的最大高度
    // 预分配最大分辨率3通道彩色缓存，兼容黑白/彩色相机
    Mat rgbBuf(maxH, maxW, CV_8UC3);
    unsigned char* g_pRgbBuffer = rgbBuf.data; /* 取Mat内部像素指针给SDK使用
    rgbBuf.data：获取 Mat 内部像素数据首地址，传给 SDK CameraImageProcess 做输出缓存；
*/
    CameraPlay(hCamera);// 启动相机持续输出图像流，不调用无法取帧
   /*功能
开启相机的采集数据流，让相机硬件持续向外输出图像帧。
参数
hCamera：相机句柄，指定要启动哪一台相机。*/
    // 设置图像输出格式
    if (tCapability.sIspCapacity.bMonoSensor) 
    /*tCapability 是相机硬件能力结构体；
bMonoSensor 是布尔标记：
值为 true：相机是黑白灰度传感器，原始画面只有亮度信息，无色彩；
值为 false：相机是彩色拜耳传感器，需要 SDK 插值生成彩色图.*/
    {
        channel = 1;
        CameraSetIspOutFormat(hCamera, CAMERA_MEDIA_TYPE_MONO8);
    } 
    else {
        channel = 3;
        CameraSetIspOutFormat(hCamera, CAMERA_MEDIA_TYPE_BGR8);
    } 
      
    printf("开始采集图像，视频保存为 /home/hu/mvs.2/wu.avi，按ESC退出\n");
    while (iDisplayFrames--)//是后置自减：先拿变量当前值做循环条件判断，判断结束后数值再减 1。
    {
        if (CameraGetImageBuffer(hCamera, &sFrameInfo, &pbyBuffer, 1000) == CAMERA_STATUS_SUCCESS)
        /*阻塞式获取相机原始 Bayer 裸图像，拿到原图内存地址和当前帧的全部信息。
         hCamera：相机句柄，指定操作哪台相机；
&sFrameInfo：输出参数，把当前帧宽、高、图像格式存入帧信息结构体；
&pbyBuffer：输出参数，接收原始图像裸数据指针；
1000：超时时间，单位 ms；1 秒内没有新图像到来，函数直接返回失败。
 判断条件 == CAMERA_STATUS_SUCCESS
CAMERA_STATUS_SUCCESS 宏等于数字 0，代表成功取到图像。
只有成功拿到帧，才会执行大括号里图像处理、录像、显示代码；
超时、相机断线、数据流异常都会跳过内部逻辑。*/
        {
            // SDK图像处理输出到Mat内部内存
            CameraImageProcess(hCamera, pbyBuffer, g_pRgbBuffer, &sFrameInfo);
/*功能SDK 内置图像处理函数，做原始图像转换：
输入未处理的 Bayer 裸图 pbyBuffer，自动完成去马赛克、白平衡、伽马校正、降噪，输出可直接给 OpenCV 使用的灰度图 / BGR 彩色图。
参数：
hCamera：相机句柄；
pbyBuffer：输入，相机原始裸图缓存；
g_pRgbBuffer：输出，处理完成图像的内存指针（来自提前创建的 Mat rgbBuf.data）；
&sFrameInfo：当前帧信息，提供宽高、格式用于图像处理。
执行完这一行，g_pRgbBuffer 里存放规整的像素数据*/
            // 截取当前帧有效宽高生成显示Mat（复用rgbBuf内存，无拷贝）
            Mat matImage(
                cv::Size(sFrameInfo.iWidth, sFrameInfo.iHeight),
                sFrameInfo.uiMediaType == CAMERA_MEDIA_TYPE_MONO8 ? CV_8UC1 : CV_8UC3,
                g_pRgbBuffer
            );
            /*参数 1:定义图像尺寸，宽高实时取自当前帧信息，适配相机当前分辨率。
            参数 2：三目运算符，自动匹配图像通道类型
            如果 SDK 输出灰度图（MONO8）→ 使用 CV_8UC1：单通道 8 位灰度图像；
如果 SDK 输出彩色 BGR 图 → 使用 CV_8UC3：三通道彩色图像；
图像类型必须和缓存数据匹配，否则画面花屏、色彩错乱。
参数 3：g_pRgbBuffer
图像像素数据的内存指针。
关键特性：Mat不会复制一份新内存，直接复用前面 rgbBuf 的内存，无内存拷贝，节省性能。
matImage 只是这块内存的视图，用来窗口显示和写入视频。

            */

            // 视频录制逻辑
            if (!isRecordStart)
            /*!isRecordStart 等价于 isRecordStart == false，含义：视频还没初始化，只在第一次进入循环时执行一次。
如果不加这个判断，每来一帧都会执行 app.open()，会重复新建视频文件，视频损坏、画面丢失。*/
            {
                Size sz = matImage.size();
                app.open("/home/hu/mvs.2/wu.avi", codec, fps, sz, (channel == 3));
                if (!app.isOpened())
                {
                    printf("视频文件打开失败！检查路径 /home/hu/mvs.2 是否存在\n");
                }
                else
                {
                    printf("视频录制已开启\n");
                    isRecordStart = true;
                }
            }
            if (app.isOpened())
            {
                app.write(matImage);
            }

            // 窗口显示与按键
            imshow("display", matImage);
            char c = waitKey(5);
            if (c == 27)
            {
                printf("检测到ESC，停止采集\n");
                break;
            }

            CameraReleaseImageBuffer(hCamera, pbyBuffer);// 释放本次原始帧缓存
        }
    }

    // 释放视频写入器
    if (app.isOpened())
    {
        app.release();
        printf("视频已保存至 /home/hu/mvs.2/wu.avi\n");
    }

    CameraUnInit(hCamera);
    // 自动释放内存
    destroyAllWindows();
    return 0;
}