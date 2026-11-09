# v2 回退与 BMS 反馈仅诊断

基线：`releases/20261026-single-unlock-estop-v2/buildable-source.tar.gz`，SHA256 `1b110d3b0299f0ba503d8fd0dfbbc69b9056b7777f7c672d9762e89fe1e16e1a`。5220 个文件逐项校验通过，恢复24个v34差异文件；两个v34专用测试移至本目录before备份。历史发布包保留。

用户确认：无反馈、过期和无效数据仅诊断，不拦截解锁/行走/割刀；有效 SOC<20% 或总压<48V 的原有禁刀保留。低电已确认后丢失反馈仍禁刀。急停/碰撞/倾斜/断链/控制权及独立启刀确认不变。恢复反馈不会自动执行旧操作。

复现：目标基线的真实 DispatchRemoteConfirmation + iBUS/backend，健康数据接受合法OK、缺失数据拒绝合法OK。正式回归先于修复执行，test_unlock_dispatch.py、test_battery_protection.py、test_blade_ok_ui.py 均在指定断言失败，再实施修复并转绿。这里是已定位的策略调整，因此不再进入多假设与额外埋点阶段。

集中策略：BatteryMonitor 的真实有效性、低电去抖、启动有效低电保护和LOW锁存保持；DATA不再生成停机输出或锁存。Motion解锁派发移除BMS门槛和数据故障重新上锁路径。E0 bit6、E1 DATA原因仍诊断，E9和首页摘要排除bit6。LOW与DATA同时存在时仍提示充电并禁刀。

原0x33逻辑协议、地址10/E9 profile2恢复；本包不含v34快速操作代次方案。以新命名配套发布包交付；未刷写硬件或提交SVN，实机记录为待测。
