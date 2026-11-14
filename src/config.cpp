#include "config.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

using nlohmann::json;

static void get_num(const json& j, const char* key, double& out) {
    if (j.contains(key)) out = j.at(key).get<double>();
}
static void get_int(const json& j, const char* key, int& out) {
    if (j.contains(key)) out = j.at(key).get<int>();
}

Config Config::Load(const std::string& path) {
    Config c;
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "[config] " << path << " bulunamadi, varsayilan ayarlarla devam ediliyor.\n";
        return c;
    }

    json j;
    try {
        f >> j;
    } catch (const std::exception& e) {
        std::cerr << "[config] " << path << " okunamadi (" << e.what() << "), varsayilan ayarlarla devam ediliyor.\n";
        return c;
    }

    if (j.contains("window_title_hint")) c.window_title_hint = j.at("window_title_hint").get<std::string>();
    if (j.contains("window_title_fallback_hint"))
        c.window_title_fallback_hint = j.at("window_title_fallback_hint").get<std::string>();
    if (j.contains("discord_client_id")) c.discord_client_id = j.at("discord_client_id").get<std::string>();
    if (j.contains("discord_client_secret"))
        c.discord_client_secret = j.at("discord_client_secret").get<std::string>();
    if (j.contains("discord_bot_token")) c.discord_bot_token = j.at("discord_bot_token").get<std::string>();
    if (j.contains("telegram_bot_token")) c.telegram_bot_token = j.at("telegram_bot_token").get<std::string>();
    if (j.contains("discord_status_channel_id"))
        c.discord_status_channel_id = j.at("discord_status_channel_id").get<std::string>();
    if (j.contains("discord_invite_url")) c.discord_invite_url = j.at("discord_invite_url").get<std::string>();

    if (j.contains("color_profiles")) {
        for (const auto& pj : j.at("color_profiles")) {
            ColorProfile p;
            p.name = pj.value("name", "isimsiz");
            auto lo = pj.at("hsv_lower");
            auto hi = pj.at("hsv_upper");
            for (int i = 0; i < 3; ++i) {
                p.hsv_lower[i] = lo.at(i).get<int>();
                p.hsv_upper[i] = hi.at(i).get<int>();
            }
            c.color_profiles.push_back(p);
        }
    }
    get_num(j, "color_min_area", c.color_min_area);
    get_num(j, "color_max_area", c.color_max_area);
    get_num(j, "color_min_score", c.color_min_score);
    get_int(j, "color_max_width", c.color_max_width);
    get_int(j, "color_max_height", c.color_max_height);

    if (j.contains("templates_dirs")) {
        c.templates_dirs = j.at("templates_dirs").get<std::vector<std::string>>();
    } else if (j.contains("templates_dir")) {
        // Eski (TEK klasor) config anahtariyla geriye donuk uyumluluk.
        c.templates_dirs = {j.at("templates_dir").get<std::string>()};
    }
    if (j.contains("hp_bar_dir")) c.hp_bar_dir = j.at("hp_bar_dir").get<std::string>();
    if (j.contains("auto_loot_enabled")) c.auto_loot_enabled = j.at("auto_loot_enabled").get<bool>();
    if (j.contains("click_use_shift")) c.click_use_shift = j.at("click_use_shift").get<bool>();
    if (j.contains("input_use_postmessage")) c.input_use_postmessage = j.at("input_use_postmessage").get<bool>();
    if (j.contains("manual_scan_enabled")) c.manual_scan_enabled = j.at("manual_scan_enabled").get<bool>();
    if (j.contains("manual_scan_dir")) c.manual_scan_dir = j.at("manual_scan_dir").get<std::string>();
    if (j.contains("manual_scan_rotate_after_sec")) c.manual_scan_rotate_after_sec = j.at("manual_scan_rotate_after_sec").get<double>();
    if (j.contains("manual_scan_threshold")) c.manual_scan_threshold = j.at("manual_scan_threshold").get<double>();
    get_num(j, "auto_loot_start_grace_sec", c.auto_loot_start_grace_sec);
    if (j.contains("anti_stuck_enabled")) c.anti_stuck_enabled = j.at("anti_stuck_enabled").get<bool>();
    get_num(j, "anti_stuck_seconds", c.anti_stuck_seconds);
    get_num(j, "anti_stuck_move_seconds", c.anti_stuck_move_seconds);
    get_num(j, "anti_stuck_no_hp_seconds", c.anti_stuck_no_hp_seconds);
    if (j.contains("humanize_enabled")) c.humanize_enabled = j.at("humanize_enabled").get<bool>();
    if (j.contains("click_dual_sendinput_enabled"))
        c.click_dual_sendinput_enabled = j.at("click_dual_sendinput_enabled").get<bool>();
    // KOR NOKTA + A-T PRO esikleri: acma/kapama arayuzden gelir, ama esikler
    // buradan (yeniden DERLEMEDEN) ayarlanabilsin.
    get_num(j, "min_detection_height_ratio", c.min_detection_height_ratio);
    get_num(j, "blind_spot_radius_ratio", c.blind_spot_radius_ratio);
    get_num(j, "blind_spot_offset_y_ratio", c.blind_spot_offset_y_ratio);
    get_num(j, "queue_scan_interval_sec", c.queue_scan_interval_sec);
    get_num(j, "queue_spot_expire_sec", c.queue_spot_expire_sec);
    if (j.contains("queue_scan_rotate_enabled"))
        c.queue_scan_rotate_enabled = j.at("queue_scan_rotate_enabled").get<bool>();
    get_num(j, "queue_scan_rotate_px_min", c.queue_scan_rotate_px_min);
    get_num(j, "queue_scan_rotate_px_max", c.queue_scan_rotate_px_max);
    if (j.contains("queue_scan_rotate_steps")) c.queue_scan_rotate_steps = j.at("queue_scan_rotate_steps").get<int>();
    if (j.contains("nesne_market_dismiss_enabled"))
        c.nesne_market_dismiss_enabled = j.at("nesne_market_dismiss_enabled").get<bool>();
    if (j.contains("nesne_market_template_path"))
        c.nesne_market_template_path = j.at("nesne_market_template_path").get<std::string>();
    get_num(j, "nesne_market_match_threshold", c.nesne_market_match_threshold);
    get_num(j, "nesne_market_check_interval_sec", c.nesne_market_check_interval_sec);
    get_num(j, "nesne_market_esc_cooldown_sec", c.nesne_market_esc_cooldown_sec);
    get_num(j, "at_pro_no_progress_sec", c.at_pro_no_progress_sec);
    get_num(j, "at_pro_phase1_sec", c.at_pro_phase1_sec);
    get_num(j, "at_pro_key_hold_sec", c.at_pro_key_hold_sec);
    get_num(j, "at_pro_min_progress_ratio", c.at_pro_min_progress_ratio);
    get_num(j, "at_pro_min_box_growth_ratio", c.at_pro_min_box_growth_ratio);
    get_num(j, "at_pro_rebase_shrink", c.at_pro_rebase_shrink);
    get_num(j, "at_pro_arrived_box_ratio", c.at_pro_arrived_box_ratio);
    if (j.contains("yolo_models")) {
        c.yolo_models.clear();
        for (const auto& item : j.at("yolo_models")) {
            Config::YoloModelEntry entry;
            entry.category = item.value("category", "");
            entry.path = item.value("path", "");
            entry.class_index = item.value("class_index", 0);
            entry.conf_threshold = item.value("conf_threshold", -1.0);
            if (!entry.category.empty() && !entry.path.empty()) c.yolo_models.push_back(entry);
        }
    }
    if (j.contains("yolo_all_models_every_frame"))
        c.yolo_all_models_every_frame = j.at("yolo_all_models_every_frame").get<bool>();
    if (j.contains("global_hotkeys_enabled"))
        c.global_hotkeys_enabled = j.at("global_hotkeys_enabled").get<bool>();
    if (j.contains("avoid_attach_thread_input"))
        c.avoid_attach_thread_input = j.at("avoid_attach_thread_input").get<bool>();
    get_int(j, "yolo_input_size", c.yolo_input_size);
    get_num(j, "yolo_conf_threshold", c.yolo_conf_threshold);
    get_num(j, "yolo_nms_threshold", c.yolo_nms_threshold);
    if (j.contains("yolo_only_mode")) c.yolo_only_mode = j.at("yolo_only_mode").get<bool>();

    get_num(j, "template_match_threshold", c.template_match_threshold);
    if (j.contains("template_multi_scale")) c.template_multi_scale = j.at("template_multi_scale").get<bool>();
    get_num(j, "template_scale_min", c.template_scale_min);
    get_num(j, "template_scale_max", c.template_scale_max);
    get_int(j, "template_scale_steps", c.template_scale_steps);
    get_int(j, "canny_low", c.canny_low);
    get_int(j, "canny_high", c.canny_high);
    get_int(j, "orb_min_matches", c.orb_min_matches);
    get_num(j, "orb_ratio_thresh", c.orb_ratio_thresh);
    get_int(j, "capture_min_interval_ms", c.capture_min_interval_ms);

    get_num(j, "mouse_move_duration_ms_min", c.mouse_move_duration_ms_min);
    get_num(j, "mouse_move_duration_ms_max", c.mouse_move_duration_ms_max);
    get_int(j, "pre_click_jitter_px", c.pre_click_jitter_px);
    get_num(j, "loop_idle_delay_sec_min", c.loop_idle_delay_sec_min);
    get_num(j, "loop_idle_delay_sec_max", c.loop_idle_delay_sec_max);
    get_int(j, "click_burst_count", c.click_burst_count);
    get_num(j, "click_burst_delay_sec_min", c.click_burst_delay_sec_min);
    get_num(j, "click_burst_delay_sec_max", c.click_burst_delay_sec_max);
    get_int(j, "click_jitter_px", c.click_jitter_px);
    get_int(j, "click_verify_checks", c.click_verify_checks);
    get_num(j, "click_verify_check_delay_sec", c.click_verify_check_delay_sec);

    get_int(j, "detection_stable_frames", c.detection_stable_frames);
    get_num(j, "detection_stable_radius_ratio", c.detection_stable_radius_ratio);
    get_num(j, "reject_spot_radius_ratio", c.reject_spot_radius_ratio);
    get_num(j, "reject_spot_expire_sec", c.reject_spot_expire_sec);
    get_num(j, "queue_spot_radius_ratio", c.queue_spot_radius_ratio);
    get_num(j, "queue_spot_expire_sec", c.queue_spot_expire_sec);
    if (j.contains("queue_other_visible_metins"))
        c.queue_other_visible_metins = j.at("queue_other_visible_metins").get<bool>();
    get_num(j, "target_lost_grace_sec", c.target_lost_grace_sec);
    get_num(j, "target_match_radius_ratio", c.target_match_radius_ratio);
    get_num(j, "max_target_wait_sec", c.max_target_wait_sec);
    get_num(j, "status_panel_appear_grace_sec", c.status_panel_appear_grace_sec);
    get_num(j, "status_panel_lost_grace_sec", c.status_panel_lost_grace_sec);
    get_num(j, "status_panel_name_match_threshold", c.status_panel_name_match_threshold);
    get_num(j, "loot_key_interval_sec", c.loot_key_interval_sec);
    get_num(j, "stuck_panel_timeout_sec", c.stuck_panel_timeout_sec);

    get_num(j, "farm_zone_radius_x_ratio", c.farm_zone_radius_x_ratio);
    get_num(j, "farm_zone_radius_y_ratio", c.farm_zone_radius_y_ratio);
    get_int(j, "bottom_ui_exclude_px", c.bottom_ui_exclude_px);
    get_num(j, "bottom_ui_exclude_ratio", c.bottom_ui_exclude_ratio);
    get_num(j, "name_label_match_threshold", c.name_label_match_threshold);
    get_num(j, "name_label_edge_match_threshold", c.name_label_edge_match_threshold);
    get_num(j, "name_label_masked_match_threshold", c.name_label_masked_match_threshold);
    get_num(j, "name_label_click_offset_factor", c.name_label_click_offset_factor);

    get_num(j, "camera_rotate_px_min", c.camera_rotate_px_min);
    get_num(j, "camera_rotate_px_max", c.camera_rotate_px_max);
    get_num(j, "recover_rotate_px", c.recover_rotate_px);
    get_num(j, "recover_small_rotate_px", c.recover_small_rotate_px);
    get_num(j, "camera_rotate_duration_ms_min", c.camera_rotate_duration_ms_min);
    get_num(j, "camera_rotate_duration_ms_max", c.camera_rotate_duration_ms_max);
    get_num(j, "search_action_wait_sec_min", c.search_action_wait_sec_min);
    get_num(j, "search_action_wait_sec_max", c.search_action_wait_sec_max);


    return c;
}

void Config::SaveColorProfiles(const std::string& path, const std::vector<ColorProfile>& profiles) {
    json j;
    std::ifstream in(path);
    if (in.is_open()) {
        try {
            in >> j;
        } catch (...) {
            j = json::object();
        }
        in.close();
    }

    json arr = json::array();
    for (const auto& p : profiles) {
        arr.push_back({
            {"name", p.name},
            {"hsv_lower", {p.hsv_lower[0], p.hsv_lower[1], p.hsv_lower[2]}},
            {"hsv_upper", {p.hsv_upper[0], p.hsv_upper[1], p.hsv_upper[2]}},
        });
    }
    j["color_profiles"] = arr;

    std::ofstream out(path);
    out << j.dump(2);
}
