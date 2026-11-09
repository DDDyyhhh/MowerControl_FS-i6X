# 2026-10-13 FS-i6X 项目分支同步

## 目标与来源

用户要求将当前 `MowerControl_FS-i6X` 项目放入公司 SVN：
`http://192.168.9.5:8080/svn/MowerControl/branches/MowerControl_FS-i6X/`。
目标分支此前最近变更为 r88（2026-09-14 创建）；原本地工作副本根指向 trunk，根版本为 r83，包含尚未登记及已修改文件，不能将本地目录名当成分支地址。

本次从当前本地文件建立筛选快照，在独立分支工作副本中覆盖、登记并提交。包含 control_chassis、remote_lvgl、OpenI6X、experiments、tools、docs、releases 及根目录协议说明。包括当前草袋、车轮参数、OID 查询调度等现有源码；各发布包的实机验收和候选状态以其原始说明为准。

## 排除范围

个人 .aws/.codex/.agents 配置、Git/SVN 数据库、.scratch 草稿、IDE 用户状态、下载目录、编译缓存与临时构建产物不提交。`releases/` 内已有固件及验证日志作为明确发布/候选交付物保留。
分支已有未涉及的历史文件保留。本次不修改 trunk。

## 验证

筛选源码在独立目录执行 `./tools/build_blade_ok.sh`，主机回归、底盘 Debug 与 mower-ok-release 构建及 FS-i6X 固件构建通过。生产 C 文件在 CMake/Keil 清单登记检查通过。
构建仍有当前源码已有警告；本次仅同步文件，没有修复或修改固件逻辑。FS-i6X 构建输出报告 RAM 15692/16384 字节（95%，剩余 692 字节）。新构建产物仅用于验证，不替换已有发布固件，不表示已刷写或通过硬件测试。

本次独立构建 BIN SHA-256：
- FS-i6X：`5216f98ee10c79b3225b188ca10905098610bb7ef92157baf316ce77b13ca07b`。
- 底盘：`9590cdc56434ea19dcdb02c5b035c278ec038cd46f110cdc0ceb106c76a68c4e`。

## 工作副本说明

原目录此前有 SVN 数据库写入错误记录，因此本次使用 `/tmp/mower-fsi6x-branch-20261013` 提交，原目录源码及 SVN 元数据保持不变。原目录仍指向 trunk，后续不得在该原目录直接提交到 trunk；应检出/使用指定分支工作副本继续开发。
最终修订号以 SVN 提交回执与日志为准。
