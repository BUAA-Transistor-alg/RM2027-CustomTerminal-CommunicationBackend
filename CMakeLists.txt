cmake_minimum_required(VERSION 3.16)
project(rm2027_customterminal_communicationbackend LANGUAGES CXX)
# 官方 RoboMaster 2027 通信协议 V1.0.0 (20261029)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Python ctypes 需要 dlopen 共享库，因此所有静态库都要带 -fPIC
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# ---------------------------------------------------------------------------
# 选项
# ---------------------------------------------------------------------------
# UDP 图传包头字节序：官方 PDF 未规定。默认大端（与 SharkDataSever 一致，可交叉验证）。
option(RM_UDP_HEADER_BIG_ENDIAN "UDP 8 字节包头使用大端序" ON)
option(RM_BUILD_CLIENT          "构建 MQTT 自定义终端"   ON)
option(RM_BUILD_SERVER          "构建 MQTT 协议服务器(自带 broker)" ON)
option(RM_BUILD_VIDEO           "构建 UDP 图传码流收发工具" ON)

# ---------------------------------------------------------------------------
# 依赖
# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# protobuf：官方 V1.0.0 协议要求 >= 3.15（proto3 optional）
# 优先使用 tools/build_protobuf.sh 构建到 third_party/ 的版本；找不到再退回系统
# ---------------------------------------------------------------------------
set(RM_PROTOBUF_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/protobuf-21.12"
    CACHE PATH "本地构建的 protobuf 安装前缀")
if(EXISTS "${RM_PROTOBUF_ROOT}/bin/protoc")
    set(Protobuf_ROOT "${RM_PROTOBUF_ROOT}")
    list(APPEND CMAKE_PREFIX_PATH "${RM_PROTOBUF_ROOT}")
    message(STATUS "使用本地 protobuf: ${RM_PROTOBUF_ROOT}")
else()
    message(STATUS "未找到本地 protobuf，使用系统版本")
endif()

find_package(Protobuf REQUIRED)
find_package(Threads REQUIRED)
# 图传编解码：FFmpeg（libavcodec 编解码 + libswscale 色彩/尺寸转换）
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavcodec libavformat libavutil libswscale)
message(STATUS "FFmpeg: libavcodec ${FFMPEG_libavcodec_VERSION} / libswscale ${FFMPEG_libswscale_VERSION}")

# proto3 optional 需要 >= 3.15
if(Protobuf_VERSION VERSION_LESS 3.15)
    message(FATAL_ERROR
        "protobuf ${Protobuf_VERSION} 过旧：官方 V1.0.0 协议使用 proto3 optional，需要 >= 3.15。\n"
        "  解决办法（任选其一）：\n"
        "    1) 跑一次脚本，自动下载+校验+编译安装到 third_party/（推荐，约 2 分钟）：\n"
        "         third_party/fetch_protobuf.sh\n"
        "    2) sudo apt install -y protobuf-compiler libprotobuf-dev（需发行版源 >= 3.15）\n"
        "  依赖清单与许可证见 third_party/README.md")
endif()
message(STATUS "protobuf 版本: ${Protobuf_VERSION}")

set(RM_PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/proto)
set(RM_PROTO_SRCS "")
foreach(p rm2027_client_up rm2027_client_down rm2027_scenario)
    list(APPEND RM_PROTO_SRCS
        ${RM_PROTO_DIR}/${p}.proto)
endforeach()

protobuf_generate_cpp(RM_PROTO_SRCS_PB RM_PROTO_HDRS_PB ${RM_PROTO_SRCS})
# 注意：必须用 OBJECT 库。生成代码靠静态初始化注册描述符，
# 若放进静态库且无人引用其符号，链接器会丢弃 .o，导致 generated_pool 里查不到消息。
add_library(rm2027_proto OBJECT ${RM_PROTO_SRCS_PB})
target_include_directories(rm2027_proto PUBLIC ${Protobuf_INCLUDE_DIRS} ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(rm2027_proto PUBLIC ${Protobuf_LIBRARIES})

# ---------------------------------------------------------------------------
# core：日志 + protobuf 编解码
# ---------------------------------------------------------------------------
add_library(rm2027_core STATIC
    src/common/log.cpp
    src/protocol/codec.cpp
)
target_include_directories(rm2027_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}
)
target_link_libraries(rm2027_core PUBLIC rm2027_proto Threads::Threads)
if(RM_UDP_HEADER_BIG_ENDIAN)
    target_compile_definitions(rm2027_core PUBLIC RM_UDP_HEADER_BIG_ENDIAN=1)
else()
    target_compile_definitions(rm2027_core PUBLIC RM_UDP_HEADER_BIG_ENDIAN=0)
endif()

# ---------------------------------------------------------------------------
# 对外 API 公共层：32 个 C++ 原生结构体 + 消息注册表 + 转换（src/api/messages.cpp 由
# tools/gen_api.py 从官方字段表生成）
# ---------------------------------------------------------------------------
add_library(rm2027_api STATIC
    src/api/messages.cpp
    src/api/registry.cpp
    src/api/scenario.cpp
)
target_link_libraries(rm2027_api PUBLIC rm2027_core)

# ---------------------------------------------------------------------------
# 最小 MQTT 3.1.1 broker（服务器自带，零第三方依赖）
# ---------------------------------------------------------------------------
add_library(rm2027_broker STATIC
    src/broker/mqtt_broker.cpp
)
target_link_libraries(rm2027_broker PUBLIC rm2027_core)

# ---------------------------------------------------------------------------
# 服务器对外接口
# ---------------------------------------------------------------------------
add_library(rm2027_api_server STATIC
    src/api/server.cpp
)
target_link_libraries(rm2027_api_server PUBLIC rm2027_api rm2027_broker)

if(RM_BUILD_SERVER)
    add_executable(rm_server src/server/main.cpp)
    target_link_libraries(rm_server PRIVATE rm2027_api_server rm2027_video rm2027_proto)
endif()

# ---------------------------------------------------------------------------
# UDP 图传码流
# ---------------------------------------------------------------------------
add_library(rm2027_video STATIC
    src/video/udp_header.cpp
    src/video/video_types.cpp
    src/video/hevc_encoder.cpp
    src/video/video_decoder.cpp
    src/video/video_sender.cpp
    src/video/video_receiver.cpp
)
target_link_libraries(rm2027_video PUBLIC rm2027_core Threads::Threads PkgConfig::FFMPEG)

if(RM_BUILD_VIDEO)
    add_executable(rm_video_tx src/video/main_tx.cpp)
    add_executable(rm_video_rx src/video/main_rx.cpp)
    target_link_libraries(rm_video_tx PRIVATE rm2027_video rm2027_proto)
    target_link_libraries(rm_video_rx PRIVATE rm2027_video rm2027_proto)
endif()

# ---------------------------------------------------------------------------
# 客户端对外接口（Paho MQTT C++）
# ---------------------------------------------------------------------------
if(RM_BUILD_CLIENT)
    find_package(PahoMqttCpp QUIET)
    find_package(eclipse-paho-mqtt-cpp QUIET)
    if(PahoMqttCpp_FOUND)
        set(RM_PAHO_TARGET PahoMqttCpp::paho-mqttpp3)
    elseif(eclipse-paho-mqtt-cpp_FOUND)
        set(RM_PAHO_TARGET eclipse-paho-mqtt-cpp::paho-mqttpp3)
    else()
        find_path(PAHO_CPP_INCLUDE_DIR mqtt/async_client.h
            PATHS /usr/include /usr/local/include)
        find_library(PAHO_CPP_LIB NAMES paho-mqttpp3
            PATHS /usr/lib /usr/lib/x86_64-linux-gnu /usr/local/lib)
        find_library(PAHO_C_LIB NAMES paho-mqtt3as paho-mqtt3a
            PATHS /usr/lib /usr/lib/x86_64-linux-gnu /usr/local/lib)
        if(PAHO_CPP_INCLUDE_DIR AND PAHO_CPP_LIB AND PAHO_C_LIB)
            add_library(paho_mqtt_cpp INTERFACE)
            target_include_directories(paho_mqtt_cpp INTERFACE ${PAHO_CPP_INCLUDE_DIR})
            target_link_libraries(paho_mqtt_cpp INTERFACE ${PAHO_CPP_LIB} ${PAHO_C_LIB})
            set(RM_PAHO_TARGET paho_mqtt_cpp)
        endif()
    endif()

    if(RM_PAHO_TARGET)
        add_library(rm2027_api_client STATIC src/api/client.cpp)
        target_link_libraries(rm2027_api_client PUBLIC rm2027_api ${RM_PAHO_TARGET})

        add_executable(rm_client src/client/main.cpp)
        target_link_libraries(rm_client PRIVATE rm2027_api_client rm2027_video rm2027_proto)
        message(STATUS "rm_client / rm2027_api_client: 已启用 (Paho MQTT C++)")
    else()
        message(WARNING
            "未找到 Paho MQTT C++，跳过客户端接口。\n"
            "  请安装: sudo apt install -y libpaho-mqtt-dev libpaho-mqttpp-dev")
    endif()
endif()

# ---------------------------------------------------------------------------
# 对外 API 最小示例（只用公开接口，见 examples/api_demo.cpp）
# ---------------------------------------------------------------------------
if(RM_PAHO_TARGET AND RM_BUILD_SERVER)
    add_executable(rm_api_demo examples/api_demo.cpp)
    target_link_libraries(rm_api_demo PRIVATE rm2027_api_server rm2027_api_client)
endif()

# ---------------------------------------------------------------------------
# 纯 C 接口 —— 供 ctypes / 其它语言调用（librm2027c.so）
# ---------------------------------------------------------------------------
if(RM_PAHO_TARGET)
    add_library(rm2027c SHARED src/capi/rm2027_c.cpp)
    # 图传接口也要暴露给 C/Python，所以 capi 需要链接 rm2027_video
    target_link_libraries(rm2027c PRIVATE
        rm2027_api_server rm2027_api_client rm2027_api rm2027_broker rm2027_core
        rm2027_video
    )
    target_include_directories(rm2027c PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_target_properties(rm2027c PROPERTIES OUTPUT_NAME rm2027c)
    message(STATUS "librm2027c.so: C 接口已启用")
endif()

message(STATUS "UDP 包头字节序: ${RM_UDP_HEADER_BIG_ENDIAN_STR}")
if(RM_UDP_HEADER_BIG_ENDIAN)
    message(STATUS "  -> 大端 (BIG ENDIAN)，与 SharkDataSever 一致")
else()
    message(STATUS "  -> 小端 (LITTLE ENDIAN)")
endif()
