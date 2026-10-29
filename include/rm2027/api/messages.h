// RM2027 自定义终端协议 V1.0.0 —— 对外 API 的原生结构体
//
// 本文件由 tools/gen_api.py 从官方 PDF 字段表自动生成，请勿手工修改。
//
// 每个结构体对应一条指令（MQTT topic = 结构体的 static kName）。
// 发送与回调都以 std::unique_ptr<具体结构体> 传递（移动语义）。
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "rm2027/api/message.h"

namespace rm2027 {

// ---- KeyboardMouseControl ----
struct KeyboardMouseControl : Message {
    static constexpr const char *kName = "KeyboardMouseControl";
    int32_t mouse_x{};
    int32_t mouse_y{};
    int32_t mouse_z{};
    bool left_button_down{};
    bool right_button_down{};
    uint32_t keyboard_value{};
    bool mid_button_down{};

    const char *name() const override { return kName; }
};

// ---- CustomControl ----
struct CustomControl : Message {
    static constexpr const char *kName = "CustomControl";
    std::string data{};

    const char *name() const override { return kName; }
};

// ---- GameStatus ----
struct GameStatus : Message {
    static constexpr const char *kName = "GameStatus";
    uint32_t current_round{};
    uint32_t total_rounds{};
    uint32_t red_score{};
    uint32_t blue_score{};
    uint32_t current_stage{};
    int32_t stage_countdown_sec{};
    int32_t stage_elapsed_sec{};
    bool is_paused{};
    uint32_t game_result{};
    uint32_t end_reason{};

    const char *name() const override { return kName; }
};

// ---- GlobalUnitStatus ----
struct GlobalUnitStatus : Message {
    static constexpr const char *kName = "GlobalUnitStatus";
    uint32_t base_health{};
    uint32_t base_status{};
    uint32_t base_shield{};
    uint32_t outpost_R_health{};
    uint32_t outpost_R_status{};
    uint32_t enemy_base_health{};
    uint32_t enemy_base_status{};
    uint32_t enemy_base_shield{};
    uint32_t outpost_B_health{};
    uint32_t outpost_B_status{};
    std::vector<uint32_t> robot_health{};
    std::vector<int32_t> robot_bullets{};
    uint32_t total_damage_ally{};
    uint32_t total_damage_enemy{};
    uint32_t total_CP_ally{};
    uint32_t total_CP_enemy{};

    const char *name() const override { return kName; }
};

// ---- GlobalLogisticsStatus ----
struct GlobalLogisticsStatus : Message {
    static constexpr const char *kName = "GlobalLogisticsStatus";
    uint32_t remaining_economy{};
    uint64_t total_economy_obtained{};
    uint32_t tech_level{};
    uint32_t encryption_level{};

    const char *name() const override { return kName; }
};

// ---- GlobalSpecialMechanism ----
struct GlobalSpecialMechanism : Message {
    static constexpr const char *kName = "GlobalSpecialMechanism";
    std::vector<uint32_t> mechanism_id{};
    std::vector<int32_t> mechanism_time_sec{};

    const char *name() const override { return kName; }
};

// ---- Event ----
struct Event : Message {
    static constexpr const char *kName = "Event";
    int32_t event_id{};
    std::string param{};

    const char *name() const override { return kName; }
};

// ---- RobotInjuryStat ----
struct RobotInjuryStat : Message {
    static constexpr const char *kName = "RobotInjuryStat";
    uint32_t total_damage{};
    uint32_t collision_damage{};
    uint32_t small_projectile_damage{};
    uint32_t large_projectile_damage{};
    uint32_t dart_splash_damage{};
    uint32_t module_offline_damage{};
    uint32_t offline_damage{};
    uint32_t penalty_damage{};
    uint32_t server_kill_damage{};
    uint32_t killer_id{};

    const char *name() const override { return kName; }
};

// ---- RobotRespawnStatus ----
struct RobotRespawnStatus : Message {
    static constexpr const char *kName = "RobotRespawnStatus";
    bool is_pending_respawn{};
    uint32_t total_respawn_progress{};
    uint32_t current_respawn_progress{};
    bool can_free_respawn{};
    uint32_t gold_cost_for_respawn{};
    bool can_pay_for_respawn{};

    const char *name() const override { return kName; }
};

// ---- RobotStaticStatus ----
struct RobotStaticStatus : Message {
    static constexpr const char *kName = "RobotStaticStatus";
    uint32_t connection_state{};
    uint32_t field_state{};
    uint32_t alive_state{};
    uint32_t robot_id{};
    uint32_t robot_type{};
    uint32_t performance_system_shooter{};
    uint32_t performance_system_chassis{};
    uint32_t level{};
    uint32_t max_health{};
    uint32_t max_heat{};
    float heat_cooldown_rate{};
    uint32_t max_power{};
    uint32_t max_buffer_energy{};
    uint32_t max_chassis_energy{};

    const char *name() const override { return kName; }
};

// ---- RobotDynamicStatus ----
struct RobotDynamicStatus : Message {
    static constexpr const char *kName = "RobotDynamicStatus";
    uint32_t current_health{};
    float current_heat{};
    float last_projectile_fire_rate{};
    uint32_t reserved{};
    uint32_t current_buffer_energy{};
    uint32_t current_experience{};
    uint32_t experience_for_upgrade{};
    uint32_t total_projectiles_fired{};
    uint32_t remaining_ammo{};
    bool is_out_of_combat{};
    uint32_t out_of_combat_countdown{};
    bool can_remote_heal{};
    bool can_remote_ammo{};

    const char *name() const override { return kName; }
};

// ---- RobotModuleStatus ----
struct RobotModuleStatus : Message {
    static constexpr const char *kName = "RobotModuleStatus";
    uint32_t power_manager{};
    uint32_t rfid{};
    uint32_t light_strip{};
    uint32_t small_shooter{};
    uint32_t big_shooter{};
    uint32_t uwb{};
    uint32_t armor{};
    uint32_t video_transmission{};
    uint32_t capacitor{};
    uint32_t main_controller{};
    uint32_t modular_42mm{};

    const char *name() const override { return kName; }
};

// ---- RobotPosition ----
struct RobotPosition : Message {
    static constexpr const char *kName = "RobotPosition";
    float x{};
    float y{};
    float z{};
    float yaw{};
    uint32_t robot_id{};

    const char *name() const override { return kName; }
};

// ---- Buff ----
struct Buff : Message {
    static constexpr const char *kName = "Buff";
    uint32_t robot_id{};
    uint32_t buff_type{};
    int32_t buff_level{};
    uint32_t buff_max_time{};
    uint32_t buff_left_time{};

    const char *name() const override { return kName; }
};

// ---- PenaltyInfo ----
struct PenaltyInfo : Message {
    static constexpr const char *kName = "PenaltyInfo";
    uint32_t penalty_type{};
    uint32_t penalty_effect_sec{};
    uint32_t total_penalty_num{};

    const char *name() const override { return kName; }
};

// ---- RobotPathPlanInfo ----
struct RobotPathPlanInfo : Message {
    static constexpr const char *kName = "RobotPathPlanInfo";
    uint32_t intention{};
    uint32_t start_pos_x{};
    uint32_t start_pos_y{};
    std::vector<int32_t> offset_x{};
    std::vector<int32_t> offset_y{};
    uint32_t sender_id{};

    const char *name() const override { return kName; }
};

// ---- MapClickInfo ----
struct MapClickInfo : Message {
    static constexpr const char *kName = "MapClickInfo";
    uint32_t is_send_all{};
    std::string robot_id{};
    uint32_t mode{};
    uint32_t enemy_id{};
    uint32_t ascii{};
    uint32_t type{};
    float map_x{};
    float map_y{};

    const char *name() const override { return kName; }
};

// ---- MapClickCmd ----
struct MapClickCmd : Message {
    static constexpr const char *kName = "MapClickCmd";
    uint32_t is_send_all{};
    std::string robot_id{};
    uint32_t mode{};
    uint32_t enemy_id{};
    uint32_t ascii{};
    uint32_t type{};
    float map_x{};
    float map_y{};

    const char *name() const override { return kName; }
};

// ---- RadarInfoToClient ----
struct RadarInfoToClient : Message {
    static constexpr const char *kName = "RadarInfoToClient";

    struct RadarSingleRobotInfo {
        uint32_t target_pos_x{};
        uint32_t target_pos_y{};
        uint32_t is_high_light{};
    };

    std::vector<RadarSingleRobotInfo> radar_single_robot_info{};

    const char *name() const override { return kName; }
};

// ---- CustomByteBlock ----
struct CustomByteBlock : Message {
    static constexpr const char *kName = "CustomByteBlock";
    std::string data{};

    const char *name() const override { return kName; }
};

// ---- AssemblyCommand ----
struct AssemblyCommand : Message {
    static constexpr const char *kName = "AssemblyCommand";
    uint32_t operation{};
    uint32_t difficulty{};

    const char *name() const override { return kName; }
};

// ---- TechCoreMotionStateSync ----
struct TechCoreMotionStateSync : Message {
    static constexpr const char *kName = "TechCoreMotionStateSync";
    uint32_t maximum_difficulty_level{};
    uint32_t basic_state{};
    uint32_t putin_state{};
    uint32_t move_state{};
    uint32_t rotate_state{};
    uint32_t enemy_core_status{};
    uint32_t remain_time_all{};
    uint32_t remain_time_step{};

    const char *name() const override { return kName; }
};

// ---- RobotPerformanceSelectionCommand ----
struct RobotPerformanceSelectionCommand : Message {
    static constexpr const char *kName = "RobotPerformanceSelectionCommand";
    uint32_t shooter{};
    uint32_t chassis{};
    uint32_t sentry_control{};

    const char *name() const override { return kName; }
};

// ---- RobotPerformanceSelectionSync ----
struct RobotPerformanceSelectionSync : Message {
    static constexpr const char *kName = "RobotPerformanceSelectionSync";
    uint32_t shooter{};
    uint32_t chassis{};
    uint32_t sentry_control{};

    const char *name() const override { return kName; }
};

// ---- CommonCommand ----
struct CommonCommand : Message {
    static constexpr const char *kName = "CommonCommand";
    uint32_t cmd_type{};
    uint32_t param{};

    const char *name() const override { return kName; }
};

// ---- RuneActivateCommand ----
struct RuneActivateCommand : Message {
    static constexpr const char *kName = "RuneActivateCommand";
    uint32_t activate{};

    const char *name() const override { return kName; }
};

// ---- RuneStatusSync ----
struct RuneStatusSync : Message {
    static constexpr const char *kName = "RuneStatusSync";
    uint32_t rune_status{};
    uint32_t activated_arms{};
    float average_rings{};

    const char *name() const override { return kName; }
};

// ---- SentryStatusSync ----
struct SentryStatusSync : Message {
    static constexpr const char *kName = "SentryStatusSync";
    uint32_t posture_id{};
    bool is_weakened{};
    bool is_powered{};

    const char *name() const override { return kName; }
};

// ---- DartCommand ----
struct DartCommand : Message {
    static constexpr const char *kName = "DartCommand";
    uint32_t target_id{};
    bool open{};
    bool launch_confirm{};

    const char *name() const override { return kName; }
};

// ---- DartSelectTargetStatusSync ----
struct DartSelectTargetStatusSync : Message {
    static constexpr const char *kName = "DartSelectTargetStatusSync";
    uint32_t target_id{};
    uint32_t open{};

    const char *name() const override { return kName; }
};

// ---- SentryCtrlCommand ----
struct SentryCtrlCommand : Message {
    static constexpr const char *kName = "SentryCtrlCommand";
    uint32_t command_id{};

    const char *name() const override { return kName; }
};

// ---- SentryCtrlResult ----
struct SentryCtrlResult : Message {
    static constexpr const char *kName = "SentryCtrlResult";
    uint32_t command_id{};
    uint32_t result_code{};

    const char *name() const override { return kName; }
};

} // namespace rm2027
