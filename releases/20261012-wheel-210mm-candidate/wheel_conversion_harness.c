#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
#define DRIVER_TYPE_OID 2
#define MOTOR_DRIVER_USED 2
typedef int ExecutionOwner_t;
static bool allowed=true;
static int32_t sent[5];
static bool SafetyOwnershipGate_Authorize(int o,bool n){(void)o;(void)n;return allowed;}
static void OID_Set_Speed(uint8_t id,int32_t speed){sent[id]=speed;}
#define WHEEL_DIAMETER 0.21f
#define WHEEL_RADIUS 0.105f
#define PI 3.1415926535f
#define GEAR_RATIO 4.43f
#define TRACK_WIDTH 0.64f
#define OID_POLE_PAIRS 10
#define OID_NODE_FL 2
#define OID_NODE_FR 1
#define OID_NODE_RL 4
#define OID_NODE_RR 3
static int16_t chassis_erpm_to_speed_mmps(int32_t erpm, int8_t sign)
{
    float speed_mps = ((float)erpm * (float)sign) * PI * WHEEL_DIAMETER /
                      ((float)OID_POLE_PAIRS * GEAR_RATIO * 60.0f);
    float speed_mmps = speed_mps * 1000.0f;
    int32_t value = (speed_mmps >= 0.0f) ? (int32_t)(speed_mmps + 0.5f) : (int32_t)(speed_mmps - 0.5f);

    if (value > INT16_MAX)
        return INT16_MAX;
    if (value < INT16_MIN)
        return INT16_MIN;
    return (int16_t)value;
}
void set_chassis_velocity(ExecutionOwner_t owner, float vx, float vy, float omega)
{
    bool nonzero_intent = (vx != 0.0f) || (vy != 0.0f) || (omega != 0.0f);

    if (!SafetyOwnershipGate_Authorize(owner, nonzero_intent))
    {
        vx = 0.0f;
        vy = 0.0f;
        omega = 0.0f;
    }
    vy = 0.0f; // 强制不支持侧向移动

    float R = TRACK_WIDTH / 2.0f;

    // 计算各轮子线速度 (m/s)
    float v_fr = vx - vy - omega * R;
    float v_fl = vx + vy + omega * R;
    float v_rr = vx + vy - omega * R;
    float v_rl = vx - vy + omega * R;

    // 转换为RPM (电机输出轴 RPM = 线速度 * 60 / 轮周长)
    float rpm_fl = (v_fl * 60.0f) / (PI * WHEEL_DIAMETER);
    float rpm_fr = (v_fr * 60.0f) / (PI * WHEEL_DIAMETER);
    float rpm_rl = (v_rl * 60.0f) / (PI * WHEEL_DIAMETER);
    float rpm_rr = (v_rr * 60.0f) / (PI * WHEEL_DIAMETER);

    // 乘以减速比，得到电机本体的真实 RPM
    float motor_rpm_fl = rpm_fl * GEAR_RATIO;
    float motor_rpm_fr = rpm_fr * GEAR_RATIO;
    float motor_rpm_rl = rpm_rl * GEAR_RATIO;
    float motor_rpm_rr = rpm_rr * GEAR_RATIO;

#if MOTOR_DRIVER_USED == DRIVER_TYPE_ODRIVE
    // ODrive 接收的是 Turns/s (转/秒)，需要除以 60
    Send_Motor_speed(1, motor_rpm_fr / 60.0f, 0.0f, 0);  // 前右
    Send_Motor_speed(2, motor_rpm_rr / 60.0f, 0.0f, 0);  // 后右
    Send_Motor_speed(3, -motor_rpm_fl / 60.0f, 0.0f, 0); // 前左，物理取反
    Send_Motor_speed(4, -motor_rpm_rl / 60.0f, 0.0f, 0); // 后左，物理取反

#elif MOTOR_DRIVER_USED == DRIVER_TYPE_MSSD
    // MSSD 直接接收 RPM (整型)
    int32_t send_rpm_fr = (int32_t)(motor_rpm_fr);
    int32_t send_rpm_rr = (int32_t)(motor_rpm_rr);
    int32_t send_rpm_fl = (int32_t)(-motor_rpm_fl);
    int32_t send_rpm_rl = (int32_t)(-motor_rpm_rl);
    // Serial_Printf(SERIAL_PORT_2, "Set Vel FR: %ld RPM, RR: %ld RPM, FL: %ld RPM, RL: %ld RPM\r\n", send_rpm_fr, send_rpm_rr, send_rpm_fl, send_rpm_rl);
    //  驱动器 1 (Node 1) 控制左侧前后电机
    MSSD_Set_Velocity(MSSD_NODE_RIGHT, MSSD_CH_FRONT, send_rpm_fr);
    osDelay(1);
    MSSD_Set_Velocity(MSSD_NODE_RIGHT, MSSD_CH_REAR, send_rpm_rr);
    osDelay(1);
    MSSD_Set_Velocity(MSSD_NODE_LEFT, MSSD_CH_FRONT, send_rpm_fl);
    osDelay(1); // 适当延时，避免 CAN 总线过载
    MSSD_Set_Velocity(MSSD_NODE_LEFT, MSSD_CH_REAR, send_rpm_rl);
    osDelay(1);

#elif MOTOR_DRIVER_USED == DRIVER_TYPE_OID
    // OID 使用 erpm，公式: erpm = rpm * 极对数
    // 全部统一使正 erpm = 前进方向
    int32_t oid_erpm_fr = (int32_t)(-motor_rpm_fr * OID_POLE_PAIRS);
    int32_t oid_erpm_rr = (int32_t)(-motor_rpm_rr * OID_POLE_PAIRS);
    int32_t oid_erpm_fl = (int32_t)(motor_rpm_fl * OID_POLE_PAIRS);
    int32_t oid_erpm_rl = (int32_t)(motor_rpm_rl * OID_POLE_PAIRS);

    OID_Set_Speed(OID_NODE_FR, oid_erpm_fr);
    OID_Set_Speed(OID_NODE_RR, oid_erpm_rr);
    OID_Set_Speed(OID_NODE_FL, oid_erpm_fl);
    OID_Set_Speed(OID_NODE_RL, oid_erpm_rl);

#endif
}
int main(void){
assert(WHEEL_DIAMETER>.2099f && WHEEL_DIAMETER<.2101f);
assert(WHEEL_RADIUS>.1049f && WHEEL_RADIUS<.1051f);
assert(OID_POLE_PAIRS==10);
set_chassis_velocity(0,.1f,0,0);assert(sent[2]==402 && sent[4]==402 && sent[1]==-402 && sent[3]==-402);
assert(chassis_erpm_to_speed_mmps(sent[2],1)==100);
set_chassis_velocity(0,.4f,0,0);assert(sent[2]==1611 && sent[1]==-1611);
assert(chassis_erpm_to_speed_mmps(sent[2],1)==400);
/* One wheel rev/sec corresponds to rotor gear_ratio*60 mechanical rpm. */
assert(chassis_erpm_to_speed_mmps(2658,1)==660);
allowed=false;set_chassis_velocity(0,.4f,0,0);for(int i=1;i<5;i++)assert(sent[i]==0);
puts("PASS: actual command/feedback functions, 210mm geometry, sign and safety rejection");}
