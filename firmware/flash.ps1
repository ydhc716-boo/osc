$openocd = "C:\Users\17937\AppData\Local\Microsoft\WinGet\Packages\xpack-dev-tools.openocd-xpack_Microsoft.Winget.Source_8wekyb3d8bbwe\xpack-openocd-0.12.0-7\bin\openocd.exe"
$scripts = "C:\Users\17937\AppData\Local\Microsoft\WinGet\Packages\xpack-dev-tools.openocd-xpack_Microsoft.Winget.Source_8wekyb3d8bbwe\xpack-openocd-0.12.0-7\share\openocd\scripts"

& $openocd -s $scripts -f interface/stlink.cfg -f target/stm32g4x.cfg -c "program build/sco_firmware.elf verify reset exit"
