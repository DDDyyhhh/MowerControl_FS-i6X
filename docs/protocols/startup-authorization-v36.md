# 授权0x36快速确认

在v35安全条件和E9/profile3基础上升级双端，v35协议说明继续作为历史回退契约。正式上行为9个20ms字：action LE16、generation LE16、flags(kind低2位、UNLK位2)、规范36字节消息CRC32 LE32。重建时使用当前绑定controller/hmi、request=action、target=1、sender=1和command/status=0；保留整个规范消息CRC，不把压缩当作校验缩短。保留位非0、未绑定、CRC不符或错会话拒绝。自动握手CRC8初值为0x5a XOR role XOR 0x36，避免混刷建立授权会话。实际安全条件、旧动作拒绝、一次性token和跨会话清空不变。

成功UNLK事务在匹配Accepted且提交后实际运行心跳前进后退休，回中不发旧取消；BLAD停止仍受CH4实时中位控制。新鲜同上下文CH4阶段可在本地回中时建立候选基线，但只有底盘实际分配的新token才能弹确认，OK不提前排队。token过期未确认的割刀意图不得撤销既有行走授权。

正常中间状态不画模态框，真实阻断/超时在首页提示，不清忙、不伪造Accepted。模型实测完整确认329ms、实际执行170ms不等于硬件时延承诺；候选包及详细日志见releases/20261029-unlock-v36。
