#!/usr/bin/env python3
"""Apply the reviewed English presentation changes to the pristine local snapshot."""
import json
from pathlib import Path

variant = Path(__file__).resolve().parent
root = variant / "source"
gui = root / "OpenI6X/FS-I6X/opentx/radio/src/gui/128x64"
translations = {
    "BMS原始SOC": "BMS SOC", "中": "MID", "低": "LOW", "高": "HIGH",
    "使能": "Enabled", "保护原因": "Batt cause", "信": "RF", "倾斜": "Tilt",
    "停刀回执": "Stop ACK", "允许": "Allowed", "关": "OFF", "关闭": "Off",
    "刀速度": "Blade", "割刀反馈过期": "Blade stale", "割刀校验异常": "Blade CRC",
    "割刀档位": "Blade gear", "割刀转速": "Blade RPM", "割刀输出": "Blade out",
    "启动被拒绝": "Start denied", "失败": "Failed", "安全输入未释放": "Safety active",
    "实际速度": "Speed act", "已拒绝": "Rejected", "已接受": "Accepted",
    "已满": "Full", "已连接": "Online", "开": "OPEN", "待确认": "Pending",
    "急停": "E-STOP", "报错": "Fault", "按OK确认": "Press OK",
    "控制权未就绪": "Owner not ready", "控制权锁存": "Ownership latch",
    "故障字": "Fault hex", "数据失效": "Data stale", "整车电压": "Voltage",
    "无": "None", "无回执": "No reply", "是否解锁": "Unlock?", "最低单体": "Min cell",
    "未就绪": "Not ready", "未满": "Not full", "未知": "Unknown", "未连接": "Offline",
    "模型设置": "Model setup", "满载未知": "Full ?", "状态未知": "Unknown",
    "电压过低": "Low volt", "电池反馈异常": "Battery feedback", "电量过低": "Low SOC",
    "确认事务": "Txn", "确认启动割刀": "Start blade?", "碰撞": "Collision",
    "禁止": "Denied", "空闲": "Idle", "等待": "Wait", "等待取消": "Wait stop",
    "等待回执": "Wait ACK", "系统设置": "Radio setup", "系统锁定": "System locked",
    "维护入口": "Maintenance", "草袋--": "Bag --", "草袋关": "Bag cls",
    "草袋已满": "Bag full", "草袋开": "Bag opn", "草袋指令": "Bag cmd",
    "草袋未满": "Bag OK", "草袋未知": "Bag ?", "草袋未连接": "No bag",
    "草袋满载": "Bag full", "草袋连接": "Bag link", "行走设定": "Speed set",
    "解锁被拒绝": "Unlock denied", "请充电": "Charge", "调试信息": "Debug info",
    "车": "CAR", "车速度": "Speed", "运行许可": "Run allow", "退出返回": "EXIT: back",
    "遥": "TX", "遥控停机锁存": "Remote stop latch", "遥控超时": "Remote timeout",
    "链路状态": "Link", "锁定": "Locked", "高度": "Height",
    # Negative display assertions remain negative, in English too.
    "行走设": "Travel set", "割刀": "Cutter", "输出": "Output", "高度设": "Height set",
    "袋指令": "Bag target", "轮速反馈过期": "Wheel stale", "BMS估算": "BMS estimate",
    "保护已标定": "Protection calibrated", "保护待标定": "Protection uncalibrated",
    "保护未知": "Protection unknown", "电池异常": "Battery fault", "链路未就绪": "Link not ready",
    "行走摇杆请回中": "Center sticks", "CH4请回中": "Center CH4",
}


def translate(text):
    for chinese, english in translations.items():
        text = text.replace('"' + chinese + '"', '"' + english + '"')
    return text


user_path = gui / "mower_user_ui.h"
user = user_path.read_text()
if 'return "未连接"' not in user:
    raise SystemExit("Expected a pristine Chinese snapshot; refusing to apply twice")
user = translate(user)
user = user.replace("Production FS-i6X presentation only.", "English FS-i6X presentation only.")
user = user.replace('return command == 1 ? "OPEN" : command == 2 ? "OFF"',
                    'return command == 1 ? "OPEN" : command == 2 ? "CLOSE"')
user = user.replace("mowerUserValue(78, 0,", "mowerUserValue(84, 0,")
user = user.replace("mowerUserValue(12, 12,", "mowerUserValue(24, 12,")
user = user.replace('mowerZhText(48, 12,', 'mowerZhText(54, 12,')
user = user.replace('mowerUserValue(60, 12,', 'mowerUserValue(72, 12,')
user = user.replace('mowerZhText(66, 36,', 'mowerZhText(72, 36,')
user = user.replace('mowerUserValue(24, 48,', 'mowerUserValue(42, 48,')
user = user.replace('mowerZhText(66, 48,', 'mowerZhText(84, 48,')
user = user.replace("const coord_t x=54;", "const coord_t x=66;")
user_path.write_text(user)

view_path = gui / "view_main.cpp"
view = view_path.read_text()
start, end = view.index("static void mowerExp3Overlay("), view.index("static void mowerPage(")
overlay = translate(view[start:end])
overlay = overlay.replace("if (notice) mowerZhCenteredText(52, notice);", """if (notice) {
      // Replace the complete bottom row so the notice never overprints values.
      lcdDrawSolidFilledRect(0, 48, 128, 16, ERASE);
      mowerZhCenteredText(52, notice);
    }""")
view_path.write_text(view[:start] + overlay + view[end:])

test_path = root / "tools/test_blade_ok_ui.py"
test = translate(test_path.read_text())
test = test.replace('mowerZhTextWidth("Speed")==33', 'mowerZhTextWidth("Speed")==30')
test = test.replace('mowerZhTextWidth("Press OK")==45', 'mowerZhTextWidth("Press OK")==48')
test = test.replace(',66,36)', ',72,36)').replace(',66,48)', ',84,48)')
test = test.replace('x=66;x<110', 'x=72;x<120').replace('x<66 || x>=110', 'x<72 || x>=120')
test = test.replace('Entire 44px cell', 'Entire 48px cell')
test = test.replace('    for(int x=72;x<120;++x) assert(pixels[47][x]); // Entire 48px cell inverted.', '''    // ASCII descenders occupy the last bitmap row: compare every inverted bit.
    uint8_t fullPixels[64][128]; memcpy(fullPixels,pixels,sizeof(pixels));
    memset(pixels,0,sizeof(pixels)); mowerZhDraw(72,36,"Bag full",false);
    for(int y=36;y<48;++y) for(int x=72;x<120;++x)
      assert(pixels[y][x]!=fullPixels[y][x]);
    memcpy(pixels,fullPixels,sizeof(pixels));''')
test = test.replace('hasAt("100%",12,12)', 'hasAt("100%",24,12)')
test = test.replace('hasAt("TX",48,12)', 'hasAt("TX",54,12)')
test = test.replace('hasAt("7.8V",60,12)', 'hasAt("7.8V",72,12)')
test = test.replace('unlock ? 42 : 31,20', 'unlock ? 43 : 28,20')
test = test.replace('hasAt("Press OK",41,34)', 'hasAt("Press OK",40,34)')
test = test.replace('hasAt("No reply",47,52)', 'hasAt("No reply",40,52)')
test = test.replace('11px CJK / 6px ASCII', '6px ASCII')
test = test.replace('PASS Chinese production UI', 'PASS English production UI')
test = test.replace('#include <cassert>', '#include <algorithm>\n#include <cassert>')
test = test.replace('    assert(x>=0 && y>=0 && x+w<=128 && y+h<=64);', '''    assert(x>=0 && y>=0 && x+w<=128 && y+h<=64);
    if(flags & ERASE) {
        textPositions.erase(std::remove_if(textPositions.begin(),textPositions.end(),
            [=](const TextPosition &p) {
                return p.x<x+w && x<p.x+int(p.value.size())*6 && p.y<y+h && y<p.y+12;
            }),textPositions.end());
    }''')
test = test.replace('    const char *dir=std::getenv("MOWER_UI_PREVIEW_DIR");', '''    // Check every visible text cell before exporting the actual framebuffer.
    for(size_t i=0;i<textPositions.size();++i) for(size_t j=i+1;j<textPositions.size();++j) {
        const auto &a=textPositions[i]; const auto &b=textPositions[j];
        const bool overlap=a.x<b.x+int(b.value.size())*6 && b.x<a.x+int(a.value.size())*6 &&
            a.y<b.y+12 && b.y<a.y+12;
        if(overlap) fprintf(stderr,"Overlapping text in %s: %s / %s\\n",name,a.value.c_str(),b.value.c_str());
        assert(!overlap);
    }
    const char *dir=std::getenv("MOWER_UI_PREVIEW_DIR");''')
test = test.replace('    return mowerZhDraw(x,y,value,inverse);', '''    // Missing glyphs must fail even though the firmware renderer skips them.
    for(const unsigned char *p=reinterpret_cast<const unsigned char *>(value);*p;++p) {
        assert(*p>=32 && *p<127);
        bool found=false;
        for(const auto &glyph:mowerZhGlyphs) if(glyph.code==*p) found=true;
        assert(found);
    }
    return mowerZhDraw(x,y,value,inverse);''')
test = test.replace('    puts("PASS profile-3 actual GUI:', '''    // Every DEBUG row, all fault indicators and the wire-format maxima fit.
    fixture={};fixture.telemetry_online=true;fixture.valid=0xffffU;
    fixture.soc_percent=100;fixture.voltage_centivolts=65535;
    fixture.walk_actual_cms=65535;fixture.blade_actual_rpm=65535;
    fixture.height_deci_cm=127;fixture.battery_min_mv=65535;
    fixture.radio_battery_100mv=255;fixture.radio_quality=255;
    fixture.fault_bits=0xffffU;fixture.battery_reasons=1;
    fixture.bag_connected=true;fixture.bag_full=true;fixture.bag_command=1;
    mowerUserPage=MOWER_USER_HOME;mowerExp3DisplayState=EXP3_UI_IDLE;
    resetDraw();mowerUserHome(fixture,true,15);saveScreen("home-max-values");
    for(unsigned row=0;row<MOWER_DEBUG_ROWS;++row) {
      resetDraw();mowerUserDebugRow(row,12,fixture,true,15);
      const std::string name="debug-row-"+std::to_string(row);saveScreen(name.c_str());
    }
    for(auto state:{EXP3_UI_BATTERY_LOW,EXP3_UI_SAFETY,EXP3_UI_OWNER,
                    EXP3_UI_TIMEOUT,EXP3_UI_REJECTED}) {
      for(bool unlock:{false,true}) {
        mowerUnlockDisplay=unlock;resetDraw();mowerUserHome(fixture,true,15);
        mowerExp3Overlay(state);
        const std::string name="notice-"+std::to_string(state)+(unlock ? "-unlock" : "-blade");
        saveScreen(name.c_str());
      }
    }
    puts("PASS profile-3 actual GUI:''')
test_path.write_text(test)
(variant / "validation/ui-translations.json").write_text(json.dumps(translations, ensure_ascii=False, indent=2) + "\n")
print("Applied English text and layout; retained event routing, control and protocol code")
