$ErrorActionPreference = "Stop"

$cc = if ($env:CC) { $env:CC } else { "gcc" }
$cxx = if ($env:CXX) { $env:CXX } else { "g++" }

& $cc -std=c99 -Wall -Wextra -pedantic c\garmin_ble_core.c c\test_garmin_ble_core.c -o c\garmin_ble_core_test.exe
& c\garmin_ble_core_test.exe

& $cxx -std=c++17 -Wall -Wextra -pedantic c\garmin_ble_core.c cpp\test_garmin_ble_core.cpp -o cpp\garmin_ble_cpp_test.exe
& cpp\garmin_ble_cpp_test.exe

& $cc -std=c99 -Wall -Wextra -pedantic c\garmin_ble_core.c ldc\garmin_ldc_sidecar_adapter.c ldc\test_garmin_ldc_adapter.c -o ldc\garmin_ldc_adapter_test.exe
& ldc\garmin_ldc_adapter_test.exe

& $cc -std=c99 -Wall -Wextra -pedantic c\garmin_ble_core.c ldc\garmin_ldc_sidecar_adapter.c ldc\garmin_ldc_sidecar_device.c ldc\test_garmin_ldc_sidecar_device.c -o ldc\garmin_ldc_sidecar_device_test.exe
& ldc\garmin_ldc_sidecar_device_test.exe

Remove-Item -ErrorAction SilentlyContinue `
  c\garmin_ble_core_test.exe, `
  cpp\garmin_ble_cpp_test.exe, `
  ldc\garmin_ldc_adapter_test.exe, `
  ldc\garmin_ldc_sidecar_device_test.exe
