// RM2027 自定义终端协议 V1.0.0 —— 消息注册表与 protobuf 转换
// 由 tools/gen_api.py 自动生成，请勿手工修改。
#include "rm2027/api/messages.h"

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>

#include "rm2027/api/detail/registry.h"
#include "rm2027_client_down.pb.h"
#include "rm2027_client_up.pb.h"

namespace rm2027 {
namespace {

// ================= KeyboardMouseControl =================
void encode_KeyboardMouseControl(const Message &m, rm_client_up::KeyboardMouseControl *p) {
    const auto &n = static_cast<const KeyboardMouseControl &>(m);
    p->set_mouse_x(n.mouse_x);
    p->set_mouse_y(n.mouse_y);
    p->set_mouse_z(n.mouse_z);
    p->set_left_button_down(n.left_button_down);
    p->set_right_button_down(n.right_button_down);
    p->set_keyboard_value(n.keyboard_value);
    p->set_mid_button_down(n.mid_button_down);
}

std::unique_ptr<Message> decode_KeyboardMouseControl(const rm_client_up::KeyboardMouseControl &p) {
    auto n = std::make_unique<KeyboardMouseControl>();
    n->mouse_x = p.mouse_x();
    n->mouse_y = p.mouse_y();
    n->mouse_z = p.mouse_z();
    n->left_button_down = p.left_button_down();
    n->right_button_down = p.right_button_down();
    n->keyboard_value = p.keyboard_value();
    n->mid_button_down = p.mid_button_down();
    return n;
}

// ================= CustomControl =================
void encode_CustomControl(const Message &m, rm_client_up::CustomControl *p) {
    const auto &n = static_cast<const CustomControl &>(m);
    p->set_data(n.data);
}

std::unique_ptr<Message> decode_CustomControl(const rm_client_up::CustomControl &p) {
    auto n = std::make_unique<CustomControl>();
    n->data = p.data();
    return n;
}

// ================= GameStatus =================
void encode_GameStatus(const Message &m, rm_client_down::GameStatus *p) {
    const auto &n = static_cast<const GameStatus &>(m);
    p->set_current_round(n.current_round);
    p->set_total_rounds(n.total_rounds);
    p->set_red_score(n.red_score);
    p->set_blue_score(n.blue_score);
    p->set_current_stage(n.current_stage);
    p->set_stage_countdown_sec(n.stage_countdown_sec);
    p->set_stage_elapsed_sec(n.stage_elapsed_sec);
    p->set_is_paused(n.is_paused);
    p->set_game_result(n.game_result);
    p->set_end_reason(n.end_reason);
}

std::unique_ptr<Message> decode_GameStatus(const rm_client_down::GameStatus &p) {
    auto n = std::make_unique<GameStatus>();
    n->current_round = p.current_round();
    n->total_rounds = p.total_rounds();
    n->red_score = p.red_score();
    n->blue_score = p.blue_score();
    n->current_stage = p.current_stage();
    n->stage_countdown_sec = p.stage_countdown_sec();
    n->stage_elapsed_sec = p.stage_elapsed_sec();
    n->is_paused = p.is_paused();
    n->game_result = p.game_result();
    n->end_reason = p.end_reason();
    return n;
}

// ================= GlobalUnitStatus =================
void encode_GlobalUnitStatus(const Message &m, rm_client_down::GlobalUnitStatus *p) {
    const auto &n = static_cast<const GlobalUnitStatus &>(m);
    p->set_base_health(n.base_health);
    p->set_base_status(n.base_status);
    p->set_base_shield(n.base_shield);
    p->set_outpost_r_health(n.outpost_R_health);
    p->set_outpost_r_status(n.outpost_R_status);
    p->set_enemy_base_health(n.enemy_base_health);
    p->set_enemy_base_status(n.enemy_base_status);
    p->set_enemy_base_shield(n.enemy_base_shield);
    p->set_outpost_b_health(n.outpost_B_health);
    p->set_outpost_b_status(n.outpost_B_status);
    for (const auto &e : n.robot_health) p->add_robot_health(e);
    for (const auto &e : n.robot_bullets) p->add_robot_bullets(e);
    p->set_total_damage_ally(n.total_damage_ally);
    p->set_total_damage_enemy(n.total_damage_enemy);
    p->set_total_cp_ally(n.total_CP_ally);
    p->set_total_cp_enemy(n.total_CP_enemy);
}

std::unique_ptr<Message> decode_GlobalUnitStatus(const rm_client_down::GlobalUnitStatus &p) {
    auto n = std::make_unique<GlobalUnitStatus>();
    n->base_health = p.base_health();
    n->base_status = p.base_status();
    n->base_shield = p.base_shield();
    n->outpost_R_health = p.outpost_r_health();
    n->outpost_R_status = p.outpost_r_status();
    n->enemy_base_health = p.enemy_base_health();
    n->enemy_base_status = p.enemy_base_status();
    n->enemy_base_shield = p.enemy_base_shield();
    n->outpost_B_health = p.outpost_b_health();
    n->outpost_B_status = p.outpost_b_status();
    n->robot_health.assign(p.robot_health().begin(), p.robot_health().end());
    n->robot_bullets.assign(p.robot_bullets().begin(), p.robot_bullets().end());
    n->total_damage_ally = p.total_damage_ally();
    n->total_damage_enemy = p.total_damage_enemy();
    n->total_CP_ally = p.total_cp_ally();
    n->total_CP_enemy = p.total_cp_enemy();
    return n;
}

// ================= GlobalLogisticsStatus =================
void encode_GlobalLogisticsStatus(const Message &m, rm_client_down::GlobalLogisticsStatus *p) {
    const auto &n = static_cast<const GlobalLogisticsStatus &>(m);
    p->set_remaining_economy(n.remaining_economy);
    p->set_total_economy_obtained(n.total_economy_obtained);
    p->set_tech_level(n.tech_level);
    p->set_encryption_level(n.encryption_level);
}

std::unique_ptr<Message> decode_GlobalLogisticsStatus(const rm_client_down::GlobalLogisticsStatus &p) {
    auto n = std::make_unique<GlobalLogisticsStatus>();
    n->remaining_economy = p.remaining_economy();
    n->total_economy_obtained = p.total_economy_obtained();
    n->tech_level = p.tech_level();
    n->encryption_level = p.encryption_level();
    return n;
}

// ================= GlobalSpecialMechanism =================
void encode_GlobalSpecialMechanism(const Message &m, rm_client_down::GlobalSpecialMechanism *p) {
    const auto &n = static_cast<const GlobalSpecialMechanism &>(m);
    for (const auto &e : n.mechanism_id) p->add_mechanism_id(e);
    for (const auto &e : n.mechanism_time_sec) p->add_mechanism_time_sec(e);
}

std::unique_ptr<Message> decode_GlobalSpecialMechanism(const rm_client_down::GlobalSpecialMechanism &p) {
    auto n = std::make_unique<GlobalSpecialMechanism>();
    n->mechanism_id.assign(p.mechanism_id().begin(), p.mechanism_id().end());
    n->mechanism_time_sec.assign(p.mechanism_time_sec().begin(), p.mechanism_time_sec().end());
    return n;
}

// ================= Event =================
void encode_Event(const Message &m, rm_client_down::Event *p) {
    const auto &n = static_cast<const Event &>(m);
    p->set_event_id(n.event_id);
    p->set_param(n.param);
}

std::unique_ptr<Message> decode_Event(const rm_client_down::Event &p) {
    auto n = std::make_unique<Event>();
    n->event_id = p.event_id();
    n->param = p.param();
    return n;
}

// ================= RobotInjuryStat =================
void encode_RobotInjuryStat(const Message &m, rm_client_down::RobotInjuryStat *p) {
    const auto &n = static_cast<const RobotInjuryStat &>(m);
    p->set_total_damage(n.total_damage);
    p->set_collision_damage(n.collision_damage);
    p->set_small_projectile_damage(n.small_projectile_damage);
    p->set_large_projectile_damage(n.large_projectile_damage);
    p->set_dart_splash_damage(n.dart_splash_damage);
    p->set_module_offline_damage(n.module_offline_damage);
    p->set_offline_damage(n.offline_damage);
    p->set_penalty_damage(n.penalty_damage);
    p->set_server_kill_damage(n.server_kill_damage);
    p->set_killer_id(n.killer_id);
}

std::unique_ptr<Message> decode_RobotInjuryStat(const rm_client_down::RobotInjuryStat &p) {
    auto n = std::make_unique<RobotInjuryStat>();
    n->total_damage = p.total_damage();
    n->collision_damage = p.collision_damage();
    n->small_projectile_damage = p.small_projectile_damage();
    n->large_projectile_damage = p.large_projectile_damage();
    n->dart_splash_damage = p.dart_splash_damage();
    n->module_offline_damage = p.module_offline_damage();
    n->offline_damage = p.offline_damage();
    n->penalty_damage = p.penalty_damage();
    n->server_kill_damage = p.server_kill_damage();
    n->killer_id = p.killer_id();
    return n;
}

// ================= RobotRespawnStatus =================
void encode_RobotRespawnStatus(const Message &m, rm_client_down::RobotRespawnStatus *p) {
    const auto &n = static_cast<const RobotRespawnStatus &>(m);
    p->set_is_pending_respawn(n.is_pending_respawn);
    p->set_total_respawn_progress(n.total_respawn_progress);
    p->set_current_respawn_progress(n.current_respawn_progress);
    p->set_can_free_respawn(n.can_free_respawn);
    p->set_gold_cost_for_respawn(n.gold_cost_for_respawn);
    p->set_can_pay_for_respawn(n.can_pay_for_respawn);
}

std::unique_ptr<Message> decode_RobotRespawnStatus(const rm_client_down::RobotRespawnStatus &p) {
    auto n = std::make_unique<RobotRespawnStatus>();
    n->is_pending_respawn = p.is_pending_respawn();
    n->total_respawn_progress = p.total_respawn_progress();
    n->current_respawn_progress = p.current_respawn_progress();
    n->can_free_respawn = p.can_free_respawn();
    n->gold_cost_for_respawn = p.gold_cost_for_respawn();
    n->can_pay_for_respawn = p.can_pay_for_respawn();
    return n;
}

// ================= RobotStaticStatus =================
void encode_RobotStaticStatus(const Message &m, rm_client_down::RobotStaticStatus *p) {
    const auto &n = static_cast<const RobotStaticStatus &>(m);
    p->set_connection_state(n.connection_state);
    p->set_field_state(n.field_state);
    p->set_alive_state(n.alive_state);
    p->set_robot_id(n.robot_id);
    p->set_robot_type(n.robot_type);
    p->set_performance_system_shooter(n.performance_system_shooter);
    p->set_performance_system_chassis(n.performance_system_chassis);
    p->set_level(n.level);
    p->set_max_health(n.max_health);
    p->set_max_heat(n.max_heat);
    p->set_heat_cooldown_rate(n.heat_cooldown_rate);
    p->set_max_power(n.max_power);
    p->set_max_buffer_energy(n.max_buffer_energy);
    p->set_max_chassis_energy(n.max_chassis_energy);
}

std::unique_ptr<Message> decode_RobotStaticStatus(const rm_client_down::RobotStaticStatus &p) {
    auto n = std::make_unique<RobotStaticStatus>();
    n->connection_state = p.connection_state();
    n->field_state = p.field_state();
    n->alive_state = p.alive_state();
    n->robot_id = p.robot_id();
    n->robot_type = p.robot_type();
    n->performance_system_shooter = p.performance_system_shooter();
    n->performance_system_chassis = p.performance_system_chassis();
    n->level = p.level();
    n->max_health = p.max_health();
    n->max_heat = p.max_heat();
    n->heat_cooldown_rate = p.heat_cooldown_rate();
    n->max_power = p.max_power();
    n->max_buffer_energy = p.max_buffer_energy();
    n->max_chassis_energy = p.max_chassis_energy();
    return n;
}

// ================= RobotDynamicStatus =================
void encode_RobotDynamicStatus(const Message &m, rm_client_down::RobotDynamicStatus *p) {
    const auto &n = static_cast<const RobotDynamicStatus &>(m);
    p->set_current_health(n.current_health);
    p->set_current_heat(n.current_heat);
    p->set_last_projectile_fire_rate(n.last_projectile_fire_rate);
    p->set_reserved(n.reserved);
    p->set_current_buffer_energy(n.current_buffer_energy);
    p->set_current_experience(n.current_experience);
    p->set_experience_for_upgrade(n.experience_for_upgrade);
    p->set_total_projectiles_fired(n.total_projectiles_fired);
    p->set_remaining_ammo(n.remaining_ammo);
    p->set_is_out_of_combat(n.is_out_of_combat);
    p->set_out_of_combat_countdown(n.out_of_combat_countdown);
    p->set_can_remote_heal(n.can_remote_heal);
    p->set_can_remote_ammo(n.can_remote_ammo);
}

std::unique_ptr<Message> decode_RobotDynamicStatus(const rm_client_down::RobotDynamicStatus &p) {
    auto n = std::make_unique<RobotDynamicStatus>();
    n->current_health = p.current_health();
    n->current_heat = p.current_heat();
    n->last_projectile_fire_rate = p.last_projectile_fire_rate();
    n->reserved = p.reserved();
    n->current_buffer_energy = p.current_buffer_energy();
    n->current_experience = p.current_experience();
    n->experience_for_upgrade = p.experience_for_upgrade();
    n->total_projectiles_fired = p.total_projectiles_fired();
    n->remaining_ammo = p.remaining_ammo();
    n->is_out_of_combat = p.is_out_of_combat();
    n->out_of_combat_countdown = p.out_of_combat_countdown();
    n->can_remote_heal = p.can_remote_heal();
    n->can_remote_ammo = p.can_remote_ammo();
    return n;
}

// ================= RobotModuleStatus =================
void encode_RobotModuleStatus(const Message &m, rm_client_down::RobotModuleStatus *p) {
    const auto &n = static_cast<const RobotModuleStatus &>(m);
    p->set_power_manager(n.power_manager);
    p->set_rfid(n.rfid);
    p->set_light_strip(n.light_strip);
    p->set_small_shooter(n.small_shooter);
    p->set_big_shooter(n.big_shooter);
    p->set_uwb(n.uwb);
    p->set_armor(n.armor);
    p->set_video_transmission(n.video_transmission);
    p->set_capacitor(n.capacitor);
    p->set_main_controller(n.main_controller);
    p->set_modular_42mm(n.modular_42mm);
}

std::unique_ptr<Message> decode_RobotModuleStatus(const rm_client_down::RobotModuleStatus &p) {
    auto n = std::make_unique<RobotModuleStatus>();
    n->power_manager = p.power_manager();
    n->rfid = p.rfid();
    n->light_strip = p.light_strip();
    n->small_shooter = p.small_shooter();
    n->big_shooter = p.big_shooter();
    n->uwb = p.uwb();
    n->armor = p.armor();
    n->video_transmission = p.video_transmission();
    n->capacitor = p.capacitor();
    n->main_controller = p.main_controller();
    n->modular_42mm = p.modular_42mm();
    return n;
}

// ================= RobotPosition =================
void encode_RobotPosition(const Message &m, rm_client_down::RobotPosition *p) {
    const auto &n = static_cast<const RobotPosition &>(m);
    p->set_x(n.x);
    p->set_y(n.y);
    p->set_z(n.z);
    p->set_yaw(n.yaw);
    p->set_robot_id(n.robot_id);
}

std::unique_ptr<Message> decode_RobotPosition(const rm_client_down::RobotPosition &p) {
    auto n = std::make_unique<RobotPosition>();
    n->x = p.x();
    n->y = p.y();
    n->z = p.z();
    n->yaw = p.yaw();
    n->robot_id = p.robot_id();
    return n;
}

// ================= Buff =================
void encode_Buff(const Message &m, rm_client_down::Buff *p) {
    const auto &n = static_cast<const Buff &>(m);
    p->set_robot_id(n.robot_id);
    p->set_buff_type(n.buff_type);
    p->set_buff_level(n.buff_level);
    p->set_buff_max_time(n.buff_max_time);
    p->set_buff_left_time(n.buff_left_time);
}

std::unique_ptr<Message> decode_Buff(const rm_client_down::Buff &p) {
    auto n = std::make_unique<Buff>();
    n->robot_id = p.robot_id();
    n->buff_type = p.buff_type();
    n->buff_level = p.buff_level();
    n->buff_max_time = p.buff_max_time();
    n->buff_left_time = p.buff_left_time();
    return n;
}

// ================= PenaltyInfo =================
void encode_PenaltyInfo(const Message &m, rm_client_down::PenaltyInfo *p) {
    const auto &n = static_cast<const PenaltyInfo &>(m);
    p->set_penalty_type(n.penalty_type);
    p->set_penalty_effect_sec(n.penalty_effect_sec);
    p->set_total_penalty_num(n.total_penalty_num);
}

std::unique_ptr<Message> decode_PenaltyInfo(const rm_client_down::PenaltyInfo &p) {
    auto n = std::make_unique<PenaltyInfo>();
    n->penalty_type = p.penalty_type();
    n->penalty_effect_sec = p.penalty_effect_sec();
    n->total_penalty_num = p.total_penalty_num();
    return n;
}

// ================= RobotPathPlanInfo =================
void encode_RobotPathPlanInfo(const Message &m, rm_client_down::RobotPathPlanInfo *p) {
    const auto &n = static_cast<const RobotPathPlanInfo &>(m);
    p->set_intention(n.intention);
    p->set_start_pos_x(n.start_pos_x);
    p->set_start_pos_y(n.start_pos_y);
    for (const auto &e : n.offset_x) p->add_offset_x(e);
    for (const auto &e : n.offset_y) p->add_offset_y(e);
    p->set_sender_id(n.sender_id);
}

std::unique_ptr<Message> decode_RobotPathPlanInfo(const rm_client_down::RobotPathPlanInfo &p) {
    auto n = std::make_unique<RobotPathPlanInfo>();
    n->intention = p.intention();
    n->start_pos_x = p.start_pos_x();
    n->start_pos_y = p.start_pos_y();
    n->offset_x.assign(p.offset_x().begin(), p.offset_x().end());
    n->offset_y.assign(p.offset_y().begin(), p.offset_y().end());
    n->sender_id = p.sender_id();
    return n;
}

// ================= MapClickInfo =================
void encode_MapClickInfo(const Message &m, rm_client_down::MapClickInfo *p) {
    const auto &n = static_cast<const MapClickInfo &>(m);
    p->set_is_send_all(n.is_send_all);
    p->set_robot_id(n.robot_id);
    p->set_mode(n.mode);
    p->set_enemy_id(n.enemy_id);
    p->set_ascii(n.ascii);
    p->set_type(n.type);
    p->set_map_x(n.map_x);
    p->set_map_y(n.map_y);
}

std::unique_ptr<Message> decode_MapClickInfo(const rm_client_down::MapClickInfo &p) {
    auto n = std::make_unique<MapClickInfo>();
    n->is_send_all = p.is_send_all();
    n->robot_id = p.robot_id();
    n->mode = p.mode();
    n->enemy_id = p.enemy_id();
    n->ascii = p.ascii();
    n->type = p.type();
    n->map_x = p.map_x();
    n->map_y = p.map_y();
    return n;
}

// ================= MapClickCmd =================
void encode_MapClickCmd(const Message &m, rm_client_up::MapClickCmd *p) {
    const auto &n = static_cast<const MapClickCmd &>(m);
    p->set_is_send_all(n.is_send_all);
    p->set_robot_id(n.robot_id);
    p->set_mode(n.mode);
    p->set_enemy_id(n.enemy_id);
    p->set_ascii(n.ascii);
    p->set_type(n.type);
    p->set_map_x(n.map_x);
    p->set_map_y(n.map_y);
}

std::unique_ptr<Message> decode_MapClickCmd(const rm_client_up::MapClickCmd &p) {
    auto n = std::make_unique<MapClickCmd>();
    n->is_send_all = p.is_send_all();
    n->robot_id = p.robot_id();
    n->mode = p.mode();
    n->enemy_id = p.enemy_id();
    n->ascii = p.ascii();
    n->type = p.type();
    n->map_x = p.map_x();
    n->map_y = p.map_y();
    return n;
}

// ================= RadarInfoToClient =================
void encode_RadarInfoToClient(const Message &m, rm_client_down::RadarInfoToClient *p) {
    const auto &n = static_cast<const RadarInfoToClient &>(m);
    for (const auto &e : n.radar_single_robot_info) {
        auto *x = p->add_radar_single_robot_info();
        x->set_target_pos_x(e.target_pos_x);
        x->set_target_pos_y(e.target_pos_y);
        x->set_is_high_light(e.is_high_light);
    }
}

std::unique_ptr<Message> decode_RadarInfoToClient(const rm_client_down::RadarInfoToClient &p) {
    auto n = std::make_unique<RadarInfoToClient>();
    n->radar_single_robot_info.clear();
    n->radar_single_robot_info.reserve(p.radar_single_robot_info_size());
    for (const auto &e : p.radar_single_robot_info()) {
        RadarInfoToClient::RadarSingleRobotInfo x{};
        x.target_pos_x = e.target_pos_x();
        x.target_pos_y = e.target_pos_y();
        x.is_high_light = e.is_high_light();
        n->radar_single_robot_info.push_back(std::move(x));
    }
    return n;
}

// ================= CustomByteBlock =================
void encode_CustomByteBlock(const Message &m, rm_client_down::CustomByteBlock *p) {
    const auto &n = static_cast<const CustomByteBlock &>(m);
    p->set_data(n.data);
}

std::unique_ptr<Message> decode_CustomByteBlock(const rm_client_down::CustomByteBlock &p) {
    auto n = std::make_unique<CustomByteBlock>();
    n->data = p.data();
    return n;
}

// ================= AssemblyCommand =================
void encode_AssemblyCommand(const Message &m, rm_client_up::AssemblyCommand *p) {
    const auto &n = static_cast<const AssemblyCommand &>(m);
    p->set_operation(n.operation);
    p->set_difficulty(n.difficulty);
}

std::unique_ptr<Message> decode_AssemblyCommand(const rm_client_up::AssemblyCommand &p) {
    auto n = std::make_unique<AssemblyCommand>();
    n->operation = p.operation();
    n->difficulty = p.difficulty();
    return n;
}

// ================= TechCoreMotionStateSync =================
void encode_TechCoreMotionStateSync(const Message &m, rm_client_down::TechCoreMotionStateSync *p) {
    const auto &n = static_cast<const TechCoreMotionStateSync &>(m);
    p->set_maximum_difficulty_level(n.maximum_difficulty_level);
    p->set_basic_state(n.basic_state);
    p->set_putin_state(n.putin_state);
    p->set_move_state(n.move_state);
    p->set_rotate_state(n.rotate_state);
    p->set_enemy_core_status(n.enemy_core_status);
    p->set_remain_time_all(n.remain_time_all);
    p->set_remain_time_step(n.remain_time_step);
}

std::unique_ptr<Message> decode_TechCoreMotionStateSync(const rm_client_down::TechCoreMotionStateSync &p) {
    auto n = std::make_unique<TechCoreMotionStateSync>();
    n->maximum_difficulty_level = p.maximum_difficulty_level();
    n->basic_state = p.basic_state();
    n->putin_state = p.putin_state();
    n->move_state = p.move_state();
    n->rotate_state = p.rotate_state();
    n->enemy_core_status = p.enemy_core_status();
    n->remain_time_all = p.remain_time_all();
    n->remain_time_step = p.remain_time_step();
    return n;
}

// ================= RobotPerformanceSelectionCommand =================
void encode_RobotPerformanceSelectionCommand(const Message &m, rm_client_up::RobotPerformanceSelectionCommand *p) {
    const auto &n = static_cast<const RobotPerformanceSelectionCommand &>(m);
    p->set_shooter(n.shooter);
    p->set_chassis(n.chassis);
    p->set_sentry_control(n.sentry_control);
}

std::unique_ptr<Message> decode_RobotPerformanceSelectionCommand(const rm_client_up::RobotPerformanceSelectionCommand &p) {
    auto n = std::make_unique<RobotPerformanceSelectionCommand>();
    n->shooter = p.shooter();
    n->chassis = p.chassis();
    n->sentry_control = p.sentry_control();
    return n;
}

// ================= RobotPerformanceSelectionSync =================
void encode_RobotPerformanceSelectionSync(const Message &m, rm_client_down::RobotPerformanceSelectionSync *p) {
    const auto &n = static_cast<const RobotPerformanceSelectionSync &>(m);
    p->set_shooter(n.shooter);
    p->set_chassis(n.chassis);
    p->set_sentry_control(n.sentry_control);
}

std::unique_ptr<Message> decode_RobotPerformanceSelectionSync(const rm_client_down::RobotPerformanceSelectionSync &p) {
    auto n = std::make_unique<RobotPerformanceSelectionSync>();
    n->shooter = p.shooter();
    n->chassis = p.chassis();
    n->sentry_control = p.sentry_control();
    return n;
}

// ================= CommonCommand =================
void encode_CommonCommand(const Message &m, rm_client_up::CommonCommand *p) {
    const auto &n = static_cast<const CommonCommand &>(m);
    p->set_cmd_type(n.cmd_type);
    p->set_param(n.param);
}

std::unique_ptr<Message> decode_CommonCommand(const rm_client_up::CommonCommand &p) {
    auto n = std::make_unique<CommonCommand>();
    n->cmd_type = p.cmd_type();
    n->param = p.param();
    return n;
}

// ================= RuneActivateCommand =================
void encode_RuneActivateCommand(const Message &m, rm_client_up::RuneActivateCommand *p) {
    const auto &n = static_cast<const RuneActivateCommand &>(m);
    p->set_activate(n.activate);
}

std::unique_ptr<Message> decode_RuneActivateCommand(const rm_client_up::RuneActivateCommand &p) {
    auto n = std::make_unique<RuneActivateCommand>();
    n->activate = p.activate();
    return n;
}

// ================= RuneStatusSync =================
void encode_RuneStatusSync(const Message &m, rm_client_down::RuneStatusSync *p) {
    const auto &n = static_cast<const RuneStatusSync &>(m);
    p->set_rune_status(n.rune_status);
    p->set_activated_arms(n.activated_arms);
    p->set_average_rings(n.average_rings);
}

std::unique_ptr<Message> decode_RuneStatusSync(const rm_client_down::RuneStatusSync &p) {
    auto n = std::make_unique<RuneStatusSync>();
    n->rune_status = p.rune_status();
    n->activated_arms = p.activated_arms();
    n->average_rings = p.average_rings();
    return n;
}

// ================= SentryStatusSync =================
void encode_SentryStatusSync(const Message &m, rm_client_down::SentryStatusSync *p) {
    const auto &n = static_cast<const SentryStatusSync &>(m);
    p->set_posture_id(n.posture_id);
    p->set_is_weakened(n.is_weakened);
    p->set_is_powered(n.is_powered);
}

std::unique_ptr<Message> decode_SentryStatusSync(const rm_client_down::SentryStatusSync &p) {
    auto n = std::make_unique<SentryStatusSync>();
    n->posture_id = p.posture_id();
    n->is_weakened = p.is_weakened();
    n->is_powered = p.is_powered();
    return n;
}

// ================= DartCommand =================
void encode_DartCommand(const Message &m, rm_client_up::DartCommand *p) {
    const auto &n = static_cast<const DartCommand &>(m);
    p->set_target_id(n.target_id);
    p->set_open(n.open);
    p->set_launch_confirm(n.launch_confirm);
}

std::unique_ptr<Message> decode_DartCommand(const rm_client_up::DartCommand &p) {
    auto n = std::make_unique<DartCommand>();
    n->target_id = p.target_id();
    n->open = p.open();
    n->launch_confirm = p.launch_confirm();
    return n;
}

// ================= DartSelectTargetStatusSync =================
void encode_DartSelectTargetStatusSync(const Message &m, rm_client_down::DartSelectTargetStatusSync *p) {
    const auto &n = static_cast<const DartSelectTargetStatusSync &>(m);
    p->set_target_id(n.target_id);
    p->set_open(n.open);
}

std::unique_ptr<Message> decode_DartSelectTargetStatusSync(const rm_client_down::DartSelectTargetStatusSync &p) {
    auto n = std::make_unique<DartSelectTargetStatusSync>();
    n->target_id = p.target_id();
    n->open = p.open();
    return n;
}

// ================= SentryCtrlCommand =================
void encode_SentryCtrlCommand(const Message &m, rm_client_up::SentryCtrlCommand *p) {
    const auto &n = static_cast<const SentryCtrlCommand &>(m);
    p->set_command_id(n.command_id);
}

std::unique_ptr<Message> decode_SentryCtrlCommand(const rm_client_up::SentryCtrlCommand &p) {
    auto n = std::make_unique<SentryCtrlCommand>();
    n->command_id = p.command_id();
    return n;
}

// ================= SentryCtrlResult =================
void encode_SentryCtrlResult(const Message &m, rm_client_down::SentryCtrlResult *p) {
    const auto &n = static_cast<const SentryCtrlResult &>(m);
    p->set_command_id(n.command_id);
    p->set_result_code(n.result_code);
}

std::unique_ptr<Message> decode_SentryCtrlResult(const rm_client_down::SentryCtrlResult &p) {
    auto n = std::make_unique<SentryCtrlResult>();
    n->command_id = p.command_id();
    n->result_code = p.result_code();
    return n;
}

const MessageDesc kMessages[] = {
    {"KeyboardMouseControl", Direction::ClientToServer,
     rm_client_up::KeyboardMouseControl::descriptor(),
     75, 75, 0,
     "[{\"name\":\"mouse_x\",\"type\":\"int32\",\"num\":1,\"repeated\":false},{\"name\":\"mouse_y\",\"type\":\"int32\",\"num\":2,\"repeated\":false},{\"name\":\"mouse_z\",\"type\":\"int32\",\"num\":3,\"repeated\":false},{\"name\":\"left_button_down\",\"type\":\"bool\",\"num\":4,\"repeated\":false},{\"name\":\"right_button_down\",\"type\":\"bool\",\"num\":5,\"repeated\":false},{\"name\":\"keyboard_value\",\"type\":\"uint32\",\"num\":6,\"repeated\":false},{\"name\":\"mid_button_down\",\"type\":\"bool\",\"num\":7,\"repeated\":false}]",
     [](const Message &m, std::string *out) {
         rm_client_up::KeyboardMouseControl p;
         encode_KeyboardMouseControl(m, &p);
         return p.SerializeToString(out);
     },
     [](const google::protobuf::Message &p) -> std::unique_ptr<Message> {
         return decode_KeyboardMouseControl(static_cast<const rm_client_up::KeyboardMouseControl &>(p));
     },
     []() -> std::unique_ptr<Message> { return std::make_unique<KeyboardMouseControl>(); }},
    {"CustomControl", Direction::ClientToServer,
     rm_client_up::CustomControl::descriptor(),
     75, 75, 0,
     "[{\"name\":\"data\",\"type\":\"bytes\",\"num\":1,\"repeated\":false}]",
     [](const Message &m, std::string *out) {
         rm_client_up::CustomControl p;
         encode_CustomControl(m, &p);
         return p.SerializeToString(out);
     },
     [](const google::protobuf::Message &p) -> std::unique_ptr<Message> {
         return decode_CustomControl(static_cast<const rm_client_up::CustomControl &>(p));
     },
     []() -> std::unique_ptr<Message> { return std::make_unique<CustomControl>(); }},
    {"MapClickCmd", Direction::ClientToServer,
     rm_client_up::MapClickCmd::descriptor(),
     0, 0, 1,
     "[{\"name\":\"is_send_all\",\"type\":\"uint32\",\"num\":1,\"repeated\":false},{\"name\":\"robot_id\",\"type\":\"bytes\",\"num\":2,\"repeated\":false},{\"name\":\"mode\",\"type\":\"uint32\",\"num\":3,\"repeated\":false},{\"name\":\"enemy_id\",\"type\":\"uint32\",\"num\":4,\"repeated\":false},{\"name\":\"ascii\",\"type\":\"uint32\",\"num\":5,\"repeated\":false},{\"name\":\"type\",\"type\":\"uint32\",\"num\":6,\"repeated\":false},{\"name\":\"map_x\",\"type\":\"float\",\"num\":7,\"repeated\":false},{\"name\":\"map_y\",\"type\":\"float\",\"num\":8,\"repeated\":false}]",
     [](const Message &m, std::string *out) {
         rm_client_up::MapClickCmd p;
         encode_MapClickCmd(m, &p);
         return p.SerializeToString(out);
     },
     [](const google::protobuf::Message &p) -> std::unique_ptr<Message> {
         return decode_MapClickCmd(static_cast<const rm_client_up::MapClickCmd &>(p));
     },
     []() -> std::unique_ptr<Message> { return std::make_unique<MapClickCmd>(); }},
    {"AssemblyCommand", Direction::ClientToServer,
     rm_client_up::AssemblyCommand::descriptor(),
     0, 10, 1,
     "[{\"name\":\"operation\",\"type\":\"uint32\",\"num\":1,\"repeated\":false},{\"name\":\"difficulty\",\"type\":\"uint32\",\"num\":2,\"repeated\":false}]",
     [](const Message &m, std::string *out) {
         rm_client_up::AssemblyCommand p;
         encode_AssemblyCommand(m, &p);
         return p.SerializeToString(out);
     },
     [](const google::protobuf::Message &p) -> std::unique_ptr<Message> {
         return decode_AssemblyCommand(static_cast<const rm_client_up::AssemblyCommand &>(p));
     },
     []() -> std::unique_ptr<Message> { return std::make_unique<AssemblyCommand>(); }},
    {"RobotPerformanceSelectionCommand", Direction::ClientToServer,
     rm_client_up::RobotPerformanceSelectionCommand::descriptor(),
     0, 10, 1,
     "[{\"name\":\"shooter\",\"type\":\"uint32\",\"num\":1,\"repeated\":false},{\"name\":\"chassis\",\"type\":\"uint32\",\"num\":2,\"repeated\":false},{\"name\":\"sentry_control\",\"type\":\"uint32\",\"num\":3,\"repeated\":false}]",
     [](const Message &m, std::string *out) {
         rm_client_up::RobotPerformanceSelectionCommand p;
         encode_RobotPerformanceSelectionCommand(m, &p);
         return p.SerializeToString(out);
     },
     [](const google::protobuf::Message &p) -> std::unique_ptr<Message> {
         return decode_RobotPerformanceSelectionCommand(static_cast<const rm_client_up::RobotPerformanceSelectionCommand &>(p));
     },
     []() -> std::unique_ptr<Message> { return std::make_unique<RobotPerformanceSelectionCommand>(); }},
    {"CommonCommand", Direction::ClientToServer,
     rm_client_up::CommonCommand::descriptor(),
     0, 10, 1,
     "[{\"name\":\"cmd_type\",\"type\":\"uint32\",\"num\":1,\"repeated\":false},{\"name\":\"param\",\"type\":\"uint32\",\"num\":2,\"repeated\":false}]",
     [](const Message &m, std::string *out) {
         rm_client_up::CommonCommand p;
         encode_CommonCommand(m, &p);
         return p.SerializeToString(out);
     },
     [](const google::protobuf::Message &p) -> std::unique_ptr<Message> {
         return decode_CommonCommand(static_cast<const rm_client_up::CommonCommand &>(p));
     },
     []() -> std::unique_ptr<Message> { return std::make_unique<CommonCommand>(); }},
    {"RuneActivateCommand", Direction::ClientToServer,
     rm_client_up::RuneActivateCommand::descriptor(),
     0, 10, 1,
     "[{\"name\":\"activate\",\"type\":\"uint32\",\"num\":1,\"repeated\":false}]",
     [](const Message &m, std::string *out) {
         rm_client_up::RuneActivateCommand p;
         encode_RuneActivateCommand(m, &p);
         return p.SerializeToString(out);
     },
     [](const google::protobuf::Message &p) -> std::unique_ptr<Message> {
         return decode_RuneActivateCommand(static_cast<const rm_client_up::RuneActivateCommand &>(p));
     },
     []() -> std::unique_ptr<Message> { return std::make_unique<RuneActivateCommand>(); }},
    {"DartCommand", Direction::ClientToServer,
     rm_client_up::DartCommand::descriptor(),
     0, 10, 1,
     "[{\"name\":\"target_id\",\"type\":\"uint32\",\"num\":1,\"repeated\":false},{\"name\":\"open\",\"type\":\"bool\",\"num\":2,\"repeated\":false},{\"name\":\"launch_confirm\",\"type\":\"bool\",\"num\":3,\"repeated\":false}]",
     [](const Message &m, std::string *out) {
         rm_client_up::DartCommand p;
         encode_DartCommand(m, &p);
         return p.SerializeToString(out);
     },
     [](const google::protobuf::Message &p) -> std::unique_ptr<Message> {
         return decode_DartCommand(static_cast<const rm_client_up::DartCommand &>(p));
     },
     []() -> std::unique_ptr<Message> { return std::make_unique<DartCommand>(); }},
    {"SentryCtrlCommand", Direction::ClientToServer,
     rm_client_up::SentryCtrlCommand::descriptor(),
     0, 10, 1,
     "[{\"name\":\"command_id\",\"type\":\"uint32\",\"num\":1,\"repeated\":false}]",
     [](const Message &m, std::string *out) {
         rm_client_up::SentryCtrlCommand p;
         encode_SentryCtrlCommand(m, &p);
         return p.SerializeToString(out);
     },
     [](const google::protobuf::Message &p) -> std::unique_ptr<Message> {
         return decode_SentryCtrlCommand(static_cast<const rm_client_up::SentryCtrlCommand &>(p));
     },
     []() -> std::unique_ptr<Message> { return std::make_unique<SentryCtrlCommand>(); }},
    {"GameStatus", Direction::ServerToClient,
     rm_client_down::GameStatus::descriptor(),
     5, 5, 0,
     "[{\"name\":\"current_round\",\"type\":\"uint32\",\"num\":1,\"repeated\":false},{\"name\":\"total_rounds\",\"type\":\"uint32\",\"num\":2,\"repeated\":false},{\"name\":\"red_score\",\"type\":\"uint32\",\"num\":3,\"repeated\":false},{\"name\":\"blue_score\",\"type\":\"uint32\",\"num\":4,\"repeated\":false},{\"name\":\"current_stage\",\"type\":\"uint32\",\"num\":5,\"repeated\":false},{\"name\":\"stage_countdown_sec\",\"type\":\"int32\",\"num\":6,\"repeated\":false},{\"name\":\"stage_elapsed_sec\",\"type\":\"int32\",\"num\":7,\"repeated\":false},{\"name\":\"is_paused\",\"type\":\"bool\",\"num\":8,\"repeated\":false},{\"name\":\"game_result\",\"type\":\"uint32\",\"num\":9,\"repeated\":false},{\"name\":\"end_reason\",\"type\":\"uint32\",\"num\":10,\"repeated\":false}]",
     [](const Message &m, std::string *out) {
         rm_client_down::GameStatus p;
         encode_GameStatus(m, &p);
         return p.SerializeToString(out);
     },
     [](const google::protobuf::Message &p) -> std::unique_ptr<Message> {
         return decode_GameStatus(static_cast<const rm_client_down::GameStatus &>(p));
     },
     []() -> std::unique_ptr<Message> { return std::make_unique<GameStatus>(); }},
    {"GlobalUnitStatus", Direction::ServerToClient,
     rm_client_down::GlobalUnitStatus::descriptor(),
     1, 1, 0,
     "[{\"name\":\"base_health\",\"type\":\"uint32\",\"num\":1,\"repeated\":false},{\"name\":\"base_status\",\"type\":\"uint32\",\"num\":2,\"repeated\":false},{\"name\":\"base_shield\",\"type\":\"uint32\",\"num\":3,\"repeated\":false},{\"name\":\"outpost_R_health\",\"type\":\"uint32\",\"num\":4,\"repeated\":false},{\"name\":\"outpost_R_status\",\"type\":\"uint32\",\"num\":5,\"repeated\":false},{\"name\":\"enemy_base_health\",\"type\":\"uint32\",\"num\":6,\"repeated\":false},{\"name\":\"enemy_base_status\",\"type\":\"uint32\",\"num\":7,\"repeated\":false},{\"name\":\"enemy_base_shield\",\"type\":\"uint32\",\"num\":8,\"repeated\":false},{\"name\":\"outpost_B_health\",\"type\":\"uint32\",\"num\":9,\"repeated\":false},{\"name\":\"outpost_B_status\",\"type\":\"uint32\",\"num\":10,\"repeated\":false},{\"name\":\"robot_health\",\"type\":\"uint32\",\"num\":11,\"repeated\":true},{\"name\":\"robot_bullets\",\"type\":\"int32\",\"num\":12,\"repeated\":true},{\"name\":\"total_damage_ally\",\"type\":\"uint32\",\"num\":13,\"repeated\":false},{\"name\":\"total_damage_enemy\",\"type\":\"uint32\",\"num\":14,\"repeated\":false},{\"name\":\"total_CP_ally\",\"type\":\"uint32\",\"num\":15,\"repeated\":false},{\"name\":\"total_CP_enemy\",\"type\":\"uint32\",\"num\":16,\"repeated\":false}]",
     [](const Message &m, std::string *out) {
         rm_client_down::GlobalUnitStatus p;
         encode_GlobalUnitStatus(m, &p);
         return p.SerializeToString(out);
     },
     [](const google::protobuf::Message &p) -> std::unique_ptr<Message> {
         return decode_GlobalUnitStatus(static_cast<const rm_client_down::GlobalUnitStatus &>(p));
     },
     []() -> std::unique_ptr<Message> { return std::make_unique<GlobalUnitStatus>(); }},
    {"GlobalLogisticsStatus", Direction::ServerToClient,
     rm_client_down::GlobalLogisticsStatus::descriptor(),
     1, 1, 0,
     "[{\"name\":\"remaining_economy\",\"type\":\"uint32\",\"num\":1,\"repeated\":false},{\"name\":\"total_economy_obtained\",\"type\":\"uint64\",\"num\":2,\"repeated\":false},{\"name\":\"tech_level\",\"type\":\"uint32\",\"num\":3,\"repeated\":false},{\"name\":\"encryption_level\",\"type\":\"uint32\",\"num\":4,\"repeated\":false}]",
     [](const Message &m, std::string *out) {
         rm_client_down::GlobalLogisticsStatus p;
         encode_GlobalLogisticsStatus(m, &p);
         return p.SerializeToString(out);
     },
     [](const google::protobuf::Message &p) -> std::unique_ptr<Message> {
         return decode_GlobalLogisticsStatus(static_cast<const rm_client_down::GlobalLogisticsStatus &>(p));
     },
     []() -> std::unique_ptr<Message> { return std::make_unique<GlobalLogisticsStatus>(); }},
    {"GlobalSpecialMechanism", Direction::ServerToClient,
     rm_client_down::GlobalSpecialMechanism::descriptor(),
     1, 1, 0,
     "[{\"name\":\"mechanism_id\",\"type\":\"uint32\",\"num\":1,\"repeated\":true},{\"name\":\"mechanism_time_sec\",\"type\":\"int32\",\"num\":2,\"repeated\":true}]",
     [](const Message &m, std::string *out) {
         rm_client_down::GlobalSpecialMechanism p;
         encode_GlobalSpecialMechanism(m, &p);
         return p.SerializeToString(out);
     },
     [](const google::protobuf::Message &p) -> std::unique_ptr<Message> {
         return decode_GlobalSpecialMechanism(static_cast<const rm_client_down::GlobalSpecialMechanism &>(p));
     },
     []() -> std::unique_ptr<Message> { return std::make_unique<GlobalSpecialMechanism>(); }},
    {"Event", Direction::ServerToClient,
     rm_client_down::Event::descriptor(),
     0, 0, 1,
     "[{\"name\":\"event_id\",\"type\":\"int32\",\"num\":1,\"repeated\":false},{\"name\":\"param\",\"type\":\"string\",\"num\":2,\"repeated\":false}]",
     [](const Message &m, std::string *out) {
         rm_client_down::Event p;
         encode_Event(m, &p);
         return p.SerializeToString(out);
     },
     [](const google::protobuf::Message &p) -> std::unique_ptr<Message> {
         return decode_Event(static_cast<const rm_client_down::Event &>(p));
     },
     []() -> std::unique_ptr<Message> { return std::make_unique<Event>(); }},
    {"RobotInjuryStat", Direction::ServerToClient,
     rm_client_down::RobotInjuryStat::descriptor(),
     1, 1, 0,
     "[{\"name\":\"total_damage\",\"type\":\"uint32\",\"num\":1,\"repeated\":false},{\"name\":\"collision_damage\",\"type\":\"uint32\",\"num\":2,\"repeated\":false},{\"name\":\"small_projectile_damage\",\"type\":\"uint32\",\"num\":3,\"repeated\":false},{\"name\":\"large_projectile_damage\",\"type\":\"uint32\",\"num\":4,\"repeated\":false},{\"name\":\"dart_splash_damage\",\"type\":\"uint32\",\"num\":5,\"repeated\":false},{\"name\":\"module_offline_damage\",\"type\":\"uint32\",\"num\":6,\"repeated\":false},{\"name\":\"offline_damage\",\"type\":\"uint32\",\"num\":7,\"repeated\":false},{\"name\":\"penalty_damage\",\"type\":\"uint32\",\"num\":8,\"repeated\":false},{\"name\":\"server_kill_damage\",\"type\":\"uint32\",\"num\":9,\"repeated\":false},{\"name\":\"killer_id\",\"type\":\"uint32\",\"num\":10,\"repeated\":false}]",
     [](const Message &m, std::string *out) {
         rm_client_down::RobotInjuryStat p;
         encode_RobotInjuryStat(m, &p);
         return p.SerializeToString(out);
     },
     [](const google::protobuf::Message &p) -> std::unique_ptr<Message> {
         return decode_RobotInjuryStat(static_cast<const rm_client_down::RobotInjuryStat &>(p));
     },
     []() -> std::unique_ptr<Message> { return std::make_unique<RobotInjuryStat>(); }},
    {"RobotRespawnStatus", Direction::ServerToClient,
     rm_client_down::RobotRespawnStatus::descriptor(),
     1, 1, 0,
     "[{\"name\":\"is_pending_respawn\",\"type\":\"bool\",\"num\":1,\"repeated\":false},{\"name\":\"total_respawn_progress\",\"type\":\"uint32\",\"num\":2,\"repeated\":false},{\"name\":\"current_respawn_progress\",\"type\":\"uint32\",\"num\":3,\"repeated\":false},{\"name\":\"can_free_respawn\",\"type\":\"bool\",\"num\":4,\"repeated\":false},{\"name\":\"gold_cost_for_respawn\",\"type\":\"uint32\",\"num\":5,\"repeated\":false},{\"name\":\"can_pay_for_respawn\",\"type\":\"bool\",\"num\":6,\"repeated\":false}]",
     [](const Message &m, std::string *out) {
         rm_client_down::RobotRespawnStatus p;
         encode_RobotRespawnStatus(m, &p);
         return p.SerializeToString(out);
     },
     [](const google::protobuf::Message &p) -> std::unique_ptr<Message> {
         return decode_RobotRespawnStatus(static_cast<const rm_client_down::RobotRespawnStatus &>(p));
     },
     []() -> std::unique_ptr<Message> { return std::make_unique<RobotRespawnStatus>(); }},
    {"RobotStaticStatus", Direction::ServerToClient,
     rm_client_down::RobotStaticStatus::descriptor(),
     1, 1, 0,
     "[{\"name\":\"connection_state\",\"type\":\"uint32\",\"num\":1,\"repeated\":false},{\"name\":\"field_state\",\"type\":\"uint32\",\"num\":2,\"repeated\":false},{\"name\":\"alive_state\",\"type\":\"uint32\",\"num\":3,\"repeated\":false},{\"name\":\"robot_id\",\"type\":\"uint32\",\"num\":4,\"repeated\":false},{\"name\":\"robot_type\",\"type\":\"uint32\",\"num\":5,\"repeated\":false},{\"name\":\"performance_system_shooter\",\"type\":\"uint32\",\"num\":6,\"repeated\":false},{\"name\":\"performance_system_chassis\",\"type\":\"uint32\",\"num\":7,\"repeated\":false},{\"name\":\"level\",\"type\":\"uint32\",\"num\":8,\"repeated\":false},{\"name\":\"max_health\",\"type\":\"uint32\",\"num\":9,\"repeated\":false},{\"name\":\"max_heat\",\"type\":\"uint32\",\"num\":10,\"repeated\":false},{\"name\":\"heat_cooldown_rate\",\"type\":\"float\",\"num\":11,\"repeated\":false},{\"name\":\"max_power\",\"type\":\"uint32\",\"num\":12,\"repeated\":false},{\"name\":\"max_buffer_energy\",\"type\":\"uint32\",\"num\":13,\"repeated\":false},{\"name\":\"max_chassis_energy\",\"type\":\"uint32\",\"num\":14,\"repeated\":false}]",
     [](const Message &m, std::string *out) {
         rm_client_down::RobotStaticStatus p;
         encode_RobotStaticStatus(m, &p);
         return p.SerializeToString(out);
     },
     [](const google::protobuf::Message &p) -> std::unique_ptr<Message> {
         return decode_RobotStaticStatus(static_cast<const rm_client_down::RobotStaticStatus &>(p));
     },
     []() -> std::unique_ptr<Message> { return std::make_unique<RobotStaticStatus>(); }},
    {"RobotDynamicStatus", Direction::ServerToClient,
     rm_client_down::RobotDynamicStatus::descriptor(),
     10, 10, 0,
     "[{\"name\":\"current_health\",\"type\":\"uint32\",\"num\":1,\"repeated\":false},{\"name\":\"current_heat\",\"type\":\"float\",\"num\":2,\"repeated\":false},{\"name\":\"last_projectile_fire_rate\",\"type\":\"float\",\"num\":3,\"repeated\":false},{\"name\":\"reserved\",\"type\":\"uint32\",\"num\":4,\"repeated\":false},{\"name\":\"current_buffer_energy\",\"type\":\"uint32\",\"num\":5,\"repeated\":false},{\"name\":\"current_experience\",\"type\":\"uint32\",\"num\":6,\"repeated\":false},{\"name\":\"experience_for_upgrade\",\"type\":\"uint32\",\"num\":7,\"repeated\":false},{\"name\":\"total_projectiles_fired\",\"type\":\"uint32\",\"num\":8,\"repeated\":false},{\"name\":\"remaining_ammo\",\"type\":\"uint32\",\"num\":9,\"repeated\":false},{\"name\":\"is_out_of_combat\",\"type\":\"bool\",\"num\":10,\"repeated\":false},{\"name\":\"out_of_combat_countdown\",\"type\":\"uint32\",\"num\":11,\"repeated\":false},{\"name\":\"can_remote_heal\",\"type\":\"bool\",\"num\":12,\"repeated\":false},{\"name\":\"can_remote_ammo\",\"type\":\"bool\",\"num\":13,\"repeated\":false}]",
     [](const Message &m, std::string *out) {
         rm_client_down::RobotDynamicStatus p;
         encode_RobotDynamicStatus(m, &p);
         return p.SerializeToString(out);
     },
     [](const google::protobuf::Message &p) -> std::unique_ptr<Message> {
         return decode_RobotDynamicStatus(static_cast<const rm_client_down::RobotDynamicStatus &>(p));
     },
     []() -> std::unique_ptr<Message> { return std::make_unique<RobotDynamicStatus>(); }},
    {"RobotModuleStatus", Direction::ServerToClient,
     rm_client_down::RobotModuleStatus::descriptor(),
     1, 1, 0,
     "[{\"name\":\"power_manager\",\"type\":\"uint32\",\"num\":1,\"repeated\":false},{\"name\":\"rfid\",\"type\":\"uint32\",\"num\":2,\"repeated\":false},{\"name\":\"light_strip\",\"type\":\"uint32\",\"num\":3,\"repeated\":false},{\"name\":\"small_shooter\",\"type\":\"uint32\",\"num\":4,\"repeated\":false},{\"name\":\"big_shooter\",\"type\":\"uint32\",\"num\":5,\"repeated\":false},{\"name\":\"uwb\",\"type\":\"uint32\",\"num\":6,\"repeated\":false},{\"name\":\"armor\",\"type\":\"uint32\",\"num\":7,\"repeated\":false},{\"name\":\"video_transmission\",\"type\":\"uint32\",\"num\":8,\"repeated\":false},{\"name\":\"capacitor\",\"type\":\"uint32\",\"num\":9,\"repeated\":false},{\"name\":\"main_controller\",\"type\":\"uint32\",\"num\":10,\"repeated\":false},{\"name\":\"modular_42mm\",\"type\":\"uint32\",\"num\":11,\"repeated\":false}]",
     [](const Message &m, std::string *out) {
         rm_client_down::RobotModuleStatus p;
         encode_RobotModuleStatus(m, &p);
         return p.SerializeToString(out);
     },
     [](const google::protobuf::Message &p) -> std::unique_ptr<Message> {
         return decode_RobotModuleStatus(static_cast<const rm_client_down::RobotModuleStatus &>(p));
     },
     []() -> std::unique_ptr<Message> { return std::make_unique<RobotModuleStatus>(); }},
    {"RobotPosition", Direction::ServerToClient,
     rm_client_down::RobotPosition::descriptor(),
     1, 1, 0,
     "[{\"name\":\"x\",\"type\":\"float\",\"num\":1,\"repeated\":false},{\"name\":\"y\",\"type\":\"float\",\"num\":2,\"repeated\":false},{\"name\":\"z\",\"type\":\"float\",\"num\":3,\"repeated\":false},{\"name\":\"yaw\",\"type\":\"float\",\"num\":4,\"repeated\":false},{\"name\":\"robot_id\",\"type\":\"uint32\",\"num\":5,\"repeated\":false}]",
     [](const Message &m, std::string *out) {
         rm_client_down::RobotPosition p;
         encode_RobotPosition(m, &p);
         return p.SerializeToString(out);
     },
     [](const google::protobuf::Message &p) -> std::unique_ptr<Message> {
         return decode_RobotPosition(static_cast<const rm_client_down::RobotPosition &>(p));
     },
     []() -> std::unique_ptr<Message> { return std::make_unique<RobotPosition>(); }},
    {"Buff", Direction::ServerToClient,
     rm_client_down::Buff::descriptor(),
     1, 1, 0,
     "[{\"name\":\"robot_id\",\"type\":\"uint32\",\"num\":1,\"repeated\":false},{\"name\":\"buff_type\",\"type\":\"uint32\",\"num\":2,\"repeated\":false},{\"name\":\"buff_level\",\"type\":\"int32\",\"num\":3,\"repeated\":false},{\"name\":\"buff_max_time\",\"type\":\"uint32\",\"num\":4,\"repeated\":false},{\"name\":\"buff_left_time\",\"type\":\"uint32\",\"num\":5,\"repeated\":false}]",
     [](const Message &m, std::string *out) {
         rm_client_down::Buff p;
         encode_Buff(m, &p);
         return p.SerializeToString(out);
     },
     [](const google::protobuf::Message &p) -> std::unique_ptr<Message> {
         return decode_Buff(static_cast<const rm_client_down::Buff &>(p));
     },
     []() -> std::unique_ptr<Message> { return std::make_unique<Buff>(); }},
    {"PenaltyInfo", Direction::ServerToClient,
     rm_client_down::PenaltyInfo::descriptor(),
     0, 0, 1,
     "[{\"name\":\"penalty_type\",\"type\":\"uint32\",\"num\":1,\"repeated\":false},{\"name\":\"penalty_effect_sec\",\"type\":\"uint32\",\"num\":2,\"repeated\":false},{\"name\":\"total_penalty_num\",\"type\":\"uint32\",\"num\":3,\"repeated\":false}]",
     [](const Message &m, std::string *out) {
         rm_client_down::PenaltyInfo p;
         encode_PenaltyInfo(m, &p);
         return p.SerializeToString(out);
     },
     [](const google::protobuf::Message &p) -> std::unique_ptr<Message> {
         return decode_PenaltyInfo(static_cast<const rm_client_down::PenaltyInfo &>(p));
     },
     []() -> std::unique_ptr<Message> { return std::make_unique<PenaltyInfo>(); }},
    {"RobotPathPlanInfo", Direction::ServerToClient,
     rm_client_down::RobotPathPlanInfo::descriptor(),
     0, 0, 1,
     "[{\"name\":\"intention\",\"type\":\"uint32\",\"num\":1,\"repeated\":false},{\"name\":\"start_pos_x\",\"type\":\"uint32\",\"num\":2,\"repeated\":false},{\"name\":\"start_pos_y\",\"type\":\"uint32\",\"num\":3,\"repeated\":false},{\"name\":\"offset_x\",\"type\":\"int32\",\"num\":4,\"repeated\":true},{\"name\":\"offset_y\",\"type\":\"int32\",\"num\":5,\"repeated\":true},{\"name\":\"sender_id\",\"type\":\"uint32\",\"num\":6,\"repeated\":false}]",
     [](const Message &m, std::string *out) {
         rm_client_down::RobotPathPlanInfo p;
         encode_RobotPathPlanInfo(m, &p);
         return p.SerializeToString(out);
     },
     [](const google::protobuf::Message &p) -> std::unique_ptr<Message> {
         return decode_RobotPathPlanInfo(static_cast<const rm_client_down::RobotPathPlanInfo &>(p));
     },
     []() -> std::unique_ptr<Message> { return std::make_unique<RobotPathPlanInfo>(); }},
    {"MapClickInfo", Direction::ServerToClient,
     rm_client_down::MapClickInfo::descriptor(),
     0, 0, 1,
     "[{\"name\":\"is_send_all\",\"type\":\"uint32\",\"num\":1,\"repeated\":false},{\"name\":\"robot_id\",\"type\":\"bytes\",\"num\":2,\"repeated\":false},{\"name\":\"mode\",\"type\":\"uint32\",\"num\":3,\"repeated\":false},{\"name\":\"enemy_id\",\"type\":\"uint32\",\"num\":4,\"repeated\":false},{\"name\":\"ascii\",\"type\":\"uint32\",\"num\":5,\"repeated\":false},{\"name\":\"type\",\"type\":\"uint32\",\"num\":6,\"repeated\":false},{\"name\":\"map_x\",\"type\":\"float\",\"num\":7,\"repeated\":false},{\"name\":\"map_y\",\"type\":\"float\",\"num\":8,\"repeated\":false}]",
     [](const Message &m, std::string *out) {
         rm_client_down::MapClickInfo p;
         encode_MapClickInfo(m, &p);
         return p.SerializeToString(out);
     },
     [](const google::protobuf::Message &p) -> std::unique_ptr<Message> {
         return decode_MapClickInfo(static_cast<const rm_client_down::MapClickInfo &>(p));
     },
     []() -> std::unique_ptr<Message> { return std::make_unique<MapClickInfo>(); }},
    {"RadarInfoToClient", Direction::ServerToClient,
     rm_client_down::RadarInfoToClient::descriptor(),
     0, 0, 1,
     "[{\"name\":\"radar_single_robot_info\",\"type\":\"RadarSingleRobotInfo\",\"num\":1,\"repeated\":true,\"fields\":[{\"name\":\"target_pos_x\",\"type\":\"uint32\",\"num\":1,\"repeated\":false},{\"name\":\"target_pos_y\",\"type\":\"uint32\",\"num\":2,\"repeated\":false},{\"name\":\"is_high_light\",\"type\":\"uint32\",\"num\":3,\"repeated\":false}]}]",
     [](const Message &m, std::string *out) {
         rm_client_down::RadarInfoToClient p;
         encode_RadarInfoToClient(m, &p);
         return p.SerializeToString(out);
     },
     [](const google::protobuf::Message &p) -> std::unique_ptr<Message> {
         return decode_RadarInfoToClient(static_cast<const rm_client_down::RadarInfoToClient &>(p));
     },
     []() -> std::unique_ptr<Message> { return std::make_unique<RadarInfoToClient>(); }},
    {"CustomByteBlock", Direction::ServerToClient,
     rm_client_down::CustomByteBlock::descriptor(),
     50, 50, 0,
     "[{\"name\":\"data\",\"type\":\"bytes\",\"num\":1,\"repeated\":false}]",
     [](const Message &m, std::string *out) {
         rm_client_down::CustomByteBlock p;
         encode_CustomByteBlock(m, &p);
         return p.SerializeToString(out);
     },
     [](const google::protobuf::Message &p) -> std::unique_ptr<Message> {
         return decode_CustomByteBlock(static_cast<const rm_client_down::CustomByteBlock &>(p));
     },
     []() -> std::unique_ptr<Message> { return std::make_unique<CustomByteBlock>(); }},
    {"TechCoreMotionStateSync", Direction::ServerToClient,
     rm_client_down::TechCoreMotionStateSync::descriptor(),
     1, 1, 0,
     "[{\"name\":\"maximum_difficulty_level\",\"type\":\"uint32\",\"num\":1,\"repeated\":false},{\"name\":\"basic_state\",\"type\":\"uint32\",\"num\":2,\"repeated\":false},{\"name\":\"putin_state\",\"type\":\"uint32\",\"num\":3,\"repeated\":false},{\"name\":\"move_state\",\"type\":\"uint32\",\"num\":4,\"repeated\":false},{\"name\":\"rotate_state\",\"type\":\"uint32\",\"num\":5,\"repeated\":false},{\"name\":\"enemy_core_status\",\"type\":\"uint32\",\"num\":6,\"repeated\":false},{\"name\":\"remain_time_all\",\"type\":\"uint32\",\"num\":7,\"repeated\":false},{\"name\":\"remain_time_step\",\"type\":\"uint32\",\"num\":8,\"repeated\":false}]",
     [](const Message &m, std::string *out) {
         rm_client_down::TechCoreMotionStateSync p;
         encode_TechCoreMotionStateSync(m, &p);
         return p.SerializeToString(out);
     },
     [](const google::protobuf::Message &p) -> std::unique_ptr<Message> {
         return decode_TechCoreMotionStateSync(static_cast<const rm_client_down::TechCoreMotionStateSync &>(p));
     },
     []() -> std::unique_ptr<Message> { return std::make_unique<TechCoreMotionStateSync>(); }},
    {"RobotPerformanceSelectionSync", Direction::ServerToClient,
     rm_client_down::RobotPerformanceSelectionSync::descriptor(),
     1, 1, 0,
     "[{\"name\":\"shooter\",\"type\":\"uint32\",\"num\":1,\"repeated\":false},{\"name\":\"chassis\",\"type\":\"uint32\",\"num\":2,\"repeated\":false},{\"name\":\"sentry_control\",\"type\":\"uint32\",\"num\":3,\"repeated\":false}]",
     [](const Message &m, std::string *out) {
         rm_client_down::RobotPerformanceSelectionSync p;
         encode_RobotPerformanceSelectionSync(m, &p);
         return p.SerializeToString(out);
     },
     [](const google::protobuf::Message &p) -> std::unique_ptr<Message> {
         return decode_RobotPerformanceSelectionSync(static_cast<const rm_client_down::RobotPerformanceSelectionSync &>(p));
     },
     []() -> std::unique_ptr<Message> { return std::make_unique<RobotPerformanceSelectionSync>(); }},
    {"RuneStatusSync", Direction::ServerToClient,
     rm_client_down::RuneStatusSync::descriptor(),
     1, 1, 0,
     "[{\"name\":\"rune_status\",\"type\":\"uint32\",\"num\":1,\"repeated\":false},{\"name\":\"activated_arms\",\"type\":\"uint32\",\"num\":2,\"repeated\":false},{\"name\":\"average_rings\",\"type\":\"float\",\"num\":3,\"repeated\":false}]",
     [](const Message &m, std::string *out) {
         rm_client_down::RuneStatusSync p;
         encode_RuneStatusSync(m, &p);
         return p.SerializeToString(out);
     },
     [](const google::protobuf::Message &p) -> std::unique_ptr<Message> {
         return decode_RuneStatusSync(static_cast<const rm_client_down::RuneStatusSync &>(p));
     },
     []() -> std::unique_ptr<Message> { return std::make_unique<RuneStatusSync>(); }},
    {"SentryStatusSync", Direction::ServerToClient,
     rm_client_down::SentryStatusSync::descriptor(),
     1, 1, 0,
     "[{\"name\":\"posture_id\",\"type\":\"uint32\",\"num\":1,\"repeated\":false},{\"name\":\"is_weakened\",\"type\":\"bool\",\"num\":2,\"repeated\":false},{\"name\":\"is_powered\",\"type\":\"bool\",\"num\":3,\"repeated\":false}]",
     [](const Message &m, std::string *out) {
         rm_client_down::SentryStatusSync p;
         encode_SentryStatusSync(m, &p);
         return p.SerializeToString(out);
     },
     [](const google::protobuf::Message &p) -> std::unique_ptr<Message> {
         return decode_SentryStatusSync(static_cast<const rm_client_down::SentryStatusSync &>(p));
     },
     []() -> std::unique_ptr<Message> { return std::make_unique<SentryStatusSync>(); }},
    {"DartSelectTargetStatusSync", Direction::ServerToClient,
     rm_client_down::DartSelectTargetStatusSync::descriptor(),
     1, 1, 0,
     "[{\"name\":\"target_id\",\"type\":\"uint32\",\"num\":1,\"repeated\":false},{\"name\":\"open\",\"type\":\"uint32\",\"num\":2,\"repeated\":false}]",
     [](const Message &m, std::string *out) {
         rm_client_down::DartSelectTargetStatusSync p;
         encode_DartSelectTargetStatusSync(m, &p);
         return p.SerializeToString(out);
     },
     [](const google::protobuf::Message &p) -> std::unique_ptr<Message> {
         return decode_DartSelectTargetStatusSync(static_cast<const rm_client_down::DartSelectTargetStatusSync &>(p));
     },
     []() -> std::unique_ptr<Message> { return std::make_unique<DartSelectTargetStatusSync>(); }},
    {"SentryCtrlResult", Direction::ServerToClient,
     rm_client_down::SentryCtrlResult::descriptor(),
     0, 0, 1,
     "[{\"name\":\"command_id\",\"type\":\"uint32\",\"num\":1,\"repeated\":false},{\"name\":\"result_code\",\"type\":\"uint32\",\"num\":2,\"repeated\":false}]",
     [](const Message &m, std::string *out) {
         rm_client_down::SentryCtrlResult p;
         encode_SentryCtrlResult(m, &p);
         return p.SerializeToString(out);
     },
     [](const google::protobuf::Message &p) -> std::unique_ptr<Message> {
         return decode_SentryCtrlResult(static_cast<const rm_client_down::SentryCtrlResult &>(p));
     },
     []() -> std::unique_ptr<Message> { return std::make_unique<SentryCtrlResult>(); }},
};

} // namespace

const MessageDesc *messageTable(std::size_t *count) {
    *count = sizeof(kMessages) / sizeof(kMessages[0]);
    return kMessages;
}

} // namespace rm2027
