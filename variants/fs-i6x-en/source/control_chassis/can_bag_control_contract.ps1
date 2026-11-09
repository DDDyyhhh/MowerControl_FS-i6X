$source = Get-Content -Raw -Encoding UTF8 -LiteralPath 'Core/UserDev/chassis_control.c'
$lifter = Get-Content -Raw -Encoding UTF8 -LiteralPath 'Core/UserDev/lifter.c'

if ($source -notmatch '#include\s+"lifter\.h"') {
    throw 'FAIL: chassis_control.c does not depend on the lifter target-control interface.'
}

if ($source -notmatch 'Lifter_SetGrassBagTarget\s*\(\s*host_bag_control\s*\)') {
    throw 'FAIL: Byte6 is not wired to the grass-bag target-control interface.'
}

if ($source -match 'g_app\.grass_bag_status\s*=\s*host_bag_control') {
    throw 'FAIL: Byte6 still overwrites actual grass-bag status instead of requesting a target state.'
}

$targetFunction = [regex]::Match($lifter, '(?s)void Lifter_SetGrassBagTarget\(uint8_t open\).*?(?=void Lifter_ToggleGrassBag)').Value
if ([string]::IsNullOrWhiteSpace($targetFunction)) {
    throw 'FAIL: target-state implementation is missing.'
}
if ($targetFunction -notmatch 'BAG_STATE_CLOSED|BAG_STATE_CLOSING' -or
    $targetFunction -notmatch 'BAG_STATE_OPEN|BAG_STATE_OPENING') {
    throw 'FAIL: target-state implementation is not idempotent for repeated open/close frames.'
}

'PASS: Byte6 is wired as a target-state command.'
