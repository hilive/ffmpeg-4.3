## ffmpeg
base ffmpeg-4.3
基于ffmpeg-4.3

## mediacodec
Add hard mediacodec support
添加mediacodec硬编码支持

## 说明
1、兼容性问题已经验证，编码后的视频ffmpeg和各系统播放器都能正常解码；也不会出现首帧异常的情况；

2、在顺带验证mediacodec的硬解码过程中发现不少坑，计划后续将mediacodec硬解码方式从jni的方式改为直接native层交互的方式；

## 编译
编译脚本地址：https://github.com/hilive/ffmpeg-build
