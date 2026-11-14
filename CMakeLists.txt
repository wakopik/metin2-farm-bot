cmake_minimum_required(VERSION 3.21)
project(elbit_cpp CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
  message(FATAL_ERROR "Bu proje sadece Windows'ta derlenir (Windows Graphics Capture + Win32 girdi API'leri).")
endif()

find_package(OpenCV CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(httplib CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(ixwebsocket CONFIG REQUIRED)
find_package(unofficial-webview2 CONFIG REQUIRED)
# YOLO (.onnx) modellerini calistirmak icin ONNX Runtime. OpenCV'nin kendi
# DNN modulu yeni Ultralytics ONNX ciktilarindaki bazi katmanlari okuyamadi
# (bkz. src/yolo_detector.cpp).
find_package(onnxruntime CONFIG REQUIRED)

# Tum exe'lerin paylastigi goruntu yakalama / tespit / ayar kodu.
add_library(core_lib OBJECT
  src/config.cpp
  src/win_window.cpp
  src/capture.cpp
  src/color_detector.cpp
  src/template_detector.cpp
  src/auto_color_detector.cpp
  src/orb_detector.cpp
  src/status_panel_detector.cpp
  src/name_label_detector.cpp
  src/yolo_detector.cpp
)
target_link_libraries(core_lib PUBLIC
  opencv_core opencv_imgproc opencv_imgcodecs opencv_highgui opencv_features2d opencv_calib3d opencv_dnn
  nlohmann_json::nlohmann_json
  onnxruntime::onnxruntime
  d3d11 dxgi windowsapp user32
  # DwmGetWindowAttribute(DWMWA_EXTENDED_FRAME_BOUNDS) - gorunur pencere cercevesi.
  dwmapi
  # timeBeginPeriod/timeEndPeriod - akici kamera donusu icin 1ms zamanlayici.
  winmm
)
target_compile_definitions(core_lib PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(core_lib PUBLIC /await:strict /EHsc)

# --- Konsol motoru (arayuzsuz) ---
add_executable(metin2_farm_bot
  src/main.cpp
  src/input.cpp
  src/bot.cpp
  $<TARGET_OBJECTS:core_lib>
)
target_link_libraries(metin2_farm_bot PRIVATE core_lib)

# --- Renk profili toplama araci ---
add_executable(color_picker
  src/color_picker_main.cpp
  $<TARGET_OBJECTS:core_lib>
)
target_link_libraries(color_picker PRIVATE core_lib)

# --- Sablon (gorsel) / egitim verisi toplama araci ---
add_executable(template_picker
  src/template_picker_main.cpp
  $<TARGET_OBJECTS:core_lib>
)
target_link_libraries(template_picker PRIVATE core_lib)

# --- Tespit onizleme araci (tiklama yapmaz) ---
add_executable(debug_preview
  src/debug_preview_main.cpp
  $<TARGET_OBJECTS:core_lib>
)
target_link_libraries(debug_preview PRIVATE core_lib)

# --- Masaustu uygulamasi (WebView2 + HTML/CSS/JS arayuz) ---
# Yalnizca 127.0.0.1:8010'a acik yerel bir HTTP sunucusu calistirir, arayuz
# (desktop_ui/) bu sunucuyla konusur. Yonetici yetkisi ister: oyun istemcisi
# Yonetici olarak calisiyorsa Windows UIPI pencere gecisini engeller.
configure_file(
  ${CMAKE_SOURCE_DIR}/src/desktop_app.rc.in
  ${CMAKE_BINARY_DIR}/generated/desktop_app.rc
  @ONLY
)
add_executable(desktop_app
  src/desktop_app_main.cpp
  src/calibration.cpp
  src/input.cpp
  src/bot.cpp
  src/discord_bot.cpp
  src/telegram_bot.cpp
  src/win_tls_trust.cpp
  ${CMAKE_BINARY_DIR}/generated/desktop_app.rc
  $<TARGET_OBJECTS:core_lib>
)
target_link_libraries(desktop_app PRIVATE core_lib httplib::httplib unofficial::webview2::webview2 ole32 dwmapi shell32 OpenSSL::SSL OpenSSL::Crypto crypt32 ixwebsocket::ixwebsocket)
# .rc icindeki elle eklenmis manifest ile linker'in otomatik manifesti cakismasin.
target_link_options(desktop_app PRIVATE "/MANIFEST:NO")
# /EHa: catch(...) donanim kaynakli cokmeleri de yakalasin (bkz. CallWithSehGuard).
set_source_files_properties(src/desktop_app_main.cpp PROPERTIES COMPILE_OPTIONS "/EHa")
set_target_properties(desktop_app PROPERTIES WIN32_EXECUTABLE TRUE)
add_custom_command(TARGET desktop_app POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/desktop_ui
    $<TARGET_FILE_DIR:desktop_app>/desktop_ui
)

# Calisma zamaninda exe'nin yaninda olmasi gerekenler: config.json, YOLO
# modelleri, onnxruntime.dll ve sablon klasorleri (config.json'daki yollar
# calisma dizinine goredir).
set(ELBIT_DATA_DIRS models dovus savas golge kiskanclik karanlik ruh dayaniklilik hp_bar nesne_market eldiven)
foreach(target desktop_app metin2_farm_bot color_picker template_picker debug_preview)
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      ${CMAKE_SOURCE_DIR}/config.json
      $<TARGET_FILE_DIR:${target}>
  )
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:onnxruntime::onnxruntime>
      $<TARGET_FILE_DIR:${target}>
  )
  foreach(dir ${ELBIT_DATA_DIRS})
    if(EXISTS ${CMAKE_SOURCE_DIR}/${dir})
      add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/${dir}
          $<TARGET_FILE_DIR:${target}>/${dir}
      )
    endif()
  endforeach()
endforeach()
