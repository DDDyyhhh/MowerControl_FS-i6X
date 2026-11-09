$gpio = Get-Content -Raw -Encoding UTF8 -LiteralPath 'Core/Src/gpio.c'
$blade = Get-Content -Raw -Encoding UTF8 -LiteralPath 'Core/UserDev/BladeMotor.c'
$ioc = Get-Content -Raw -Encoding UTF8 -LiteralPath 'concrol.ioc'

if ($gpio -notmatch 'HAL_GPIO_WritePin\(GPIOE,\s*GPIO_PIN_5,\s*GPIO_PIN_SET\)') {
    throw 'FAIL: PE5 is not driven high during GPIO initialization.'
}

if ($gpio -notmatch '(?s)GPIO_InitStruct\.Pin\s*=\s*[^;]*GPIO_PIN_5[^;]*;.*?GPIO_InitStruct\.Mode\s*=\s*GPIO_MODE_OUTPUT_PP') {
    throw 'FAIL: PE5 is not configured as a push-pull output.'
}

if ($ioc -notmatch '(?m)^PE5\.Signal=GPIO_Output$' -or
    $ioc -notmatch '(?m)^PE5\.PinState=GPIO_PIN_SET$') {
    throw 'FAIL: concrol.ioc does not describe PE5 as a high-default output.'
}

if ($blade -notmatch 'GPIOE' -or $blade -notmatch 'GPIO_PIN_5' -or
    $blade -notmatch 'GPIO_PIN_RESET' -or $blade -notmatch 'GPIO_PIN_SET') {
    throw 'FAIL: BladeMotor does not implement the active-low PE5 switch.'
}

if ($blade -notmatch 'BladeMotor_SetSwitch\(speed_percent\s*>\s*0\.01f\s*\?\s*1\s*:\s*0\)') {
    throw 'FAIL: positive blade PWM does not enable PE5 while zero PWM disables it.'
}

if (($blade -split 'void BladeMotor_Start\(void\)', 2)[1] -notmatch 'BladeMotor_SetSwitch\(0\)' -or
    ($blade -split 'void BladeMotor_Stop\(void\)', 2)[1] -notmatch 'BladeMotor_SetSwitch\(0\)') {
    throw 'FAIL: BladeMotor start/stop paths do not force the active-low switch off safely.'
}

if (($blade -split 'void BladeMotor_Start\(void\)', 2)[1] -match 'HAL_GPIO_WritePin\(GPIOE,\s*GPIO_PIN_5,\s*GPIO_PIN_RESET\)') {
    throw 'FAIL: BladeMotor_Start briefly enables the active-low blade switch.'
}

'PASS: PE5 is configured as a high-default, active-low blade switch.'
