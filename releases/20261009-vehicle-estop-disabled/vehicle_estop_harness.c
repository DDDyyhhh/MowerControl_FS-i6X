#include <stdbool.h>
#include <assert.h>
#define GPIOD 0
#define GPIO_PIN_1 1
#define GPIO_PIN_SET 1
static int level;
static int reads;
#if VEHICLE_ESTOP_ENABLED
static int HAL_GPIO_ReadPin(int port, int pin) {assert(port==GPIOD && pin==GPIO_PIN_1); ++reads; return level;}
#endif
static bool VehicleEmergencyActive(void)
{
#if VEHICLE_ESTOP_ENABLED
  return HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_1) == GPIO_PIN_SET;
#else
  return false;
#endif
}

int main(void) {
  level=0; assert(!VehicleEmergencyActive());
  level=1; assert(VehicleEmergencyActive()==(VEHICLE_ESTOP_ENABLED!=0));
  assert(reads==(VEHICLE_ESTOP_ENABLED ? 2 : 0));
  return 0;
}
