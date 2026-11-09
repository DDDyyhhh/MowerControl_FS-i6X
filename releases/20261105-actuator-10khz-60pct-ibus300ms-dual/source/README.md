# 10kHz／60%双端固件源码审计快照

本目录保存本轮关键配置、主控和FS-i6X源码、主机验证及文档快照，不是完整独立检出。未包含全部HAL、FreeRTOS和完整遥控器工程；规范构建入口为tools/build_blade_ok.sh，Unicorn 2.1.4临时依赖位于/tmp/mower-60pct-test-deps。

任务补丁以本轮开始时15kHz/40%工作副本为基线，保留此前已有修改。921份遥控器未改源文件的核对清单在validation/radio-source-baseline.json。
