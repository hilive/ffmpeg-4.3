## ffmpeg
base ffmpeg-4.3
基于ffmpeg-4.3

## mediacodec
Add hard mediacodec support
添加mediacodec硬编码支持

## 说明
1、使用方式 avcodec_find_encoder_by_name("h264_mediacodec")
2、兼容性问题已经验证，编码后的视频ffmpeg和各系统播放器都能正常解码；也不会出现首帧异常的情况；
3、在顺带验证mediacodec的硬解码过程中发现不少坑，计划后续将mediacodec硬解码方式从jni的方式改为直接native层交互的方式；

## 注意事项
1、由于mediacodec 的sps、pps 信息开始编码的时候才会得到，所以ffmpeg 初始化的时候先用空帧编码出sps 和 pps，为了防止正式编码的时候首帧黑屏的情况，需要重启下编码器；
2、mtk 的 mediacodec 实现会有些缺陷，比如复用 mediacodec 的时候，系统内部由于异步处理的状态同步有问题会导致 复用失败。所以通过每次启动都重新创建一个 mediacodec 对象来规避；
3、native 层使用 mediacodec ，发现cq 模式 的兼容性不够好，很多机型只支持 vbr 模式，不支持cq模式，这里要注意兼容下；

## 编译
编译脚本地址：https://github.com/hilive/ffmpeg-build
