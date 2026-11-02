#pragma once

namespace obl::gui_data {

inline constexpr const char* kGuiListSpriteNamesInit[] = {
        "UI3_combobox_button",
        "UI3_combobox_button_w",
        "UI3_combobox_button_b",
};
inline constexpr const char* kGuiMainItemSpriteNamesInit[] = {
        "UI3_combobox_textarea",
        "UI3_combobox_textarea_w",
        "UI3_combobox_textarea_b",
};

inline constexpr char g_RadarUpButtonGraphic[] = "ui3_scanner_upbutton";
inline constexpr char g_RadarDownButtonGraphic[] = "ui3_scanner_downbutton";
inline constexpr char g_RadarPanelUpSprite[] = "ui3_scanner_panelup";
inline constexpr char g_RadarPanelDownSprite[] = "ui3_scanner_paneldown";
inline constexpr char g_RadarTooltipKey[] = "MAIN_TIPTEXT_RADAR";
inline constexpr char g_ProgressPanelDownSprite[] = "ui3_progress_paneldown";
inline constexpr char g_ProgressDownButtonGraphic[] = "ui3_progress_downbutton";
inline constexpr char g_ProgressTooltipKey[] = "MAIN_TIPTEXT_PROGRESS";
inline constexpr char g_GuiScrollBarSliderSprite[] = "UI3_SCROLLBAR_SLIDER";
inline constexpr char g_GuiScrollBarTrackSprite[] = "UI3_scrollbar_back";
inline constexpr char g_ScreenImagePath[] = "data\\screens\\";
inline constexpr char g_GuiSpriteBpPreview[] = "UI3_bp_preview";
inline constexpr char g_GuiSpriteBpThumb[] = "UI3_bp_thumb";
inline constexpr char g_GuiSpriteBpGenderMale[] = "UI3_bp_gender_male";
inline constexpr char g_GuiSpriteBpGenderFemale[] = "UI3_bp_gender_female";
inline constexpr char g_GuiSpriteBiSideiconMale[] = "UI3_bi_sideicon_male";
inline constexpr char g_GuiSpriteBiSideiconFemale[] = "UI3_bi_sideicon_female";
inline constexpr char g_GuiSpriteMp3Pips[] = "UI3_mp3_pips";
inline constexpr char g_GuiSpritePowerWarnings[] = "UI3_pp_warnings";
inline constexpr char g_GuiSpriteObjChecks[] = "UI3_obj_checks";
inline constexpr char g_GuiSpriteStaffCleaner[] = "ui3_st_ph_cleaner";
inline constexpr char g_GuiSpriteStaffBuilder[] = "ui3_st_ph_builder";
inline constexpr char g_GuiSpriteStaffHandyman[] = "ui3_st_ph_handy";
inline constexpr char g_GuiSpriteStaffSecurity[] = "ui3_st_ph_security";
inline constexpr char g_GuiSpriteStaffRepMale[] = "ui3_st_ph_rep_m";
inline constexpr char g_GuiSpriteStaffRepFemale[] = "ui3_st_ph_rep_f";
inline constexpr char g_GuiSpriteStaffLifeguardMale[] = "ui3_st_ph_lifeg_m";
inline constexpr char g_GuiSpriteStaffLifeguardFemale[] = "ui3_st_ph_lifeg_f";
inline constexpr const char* kGuiImageListKindNames[] = {
        "BUILD_HOTEL_SMALL",
        "BUILD_FOOD_SMALL",
        "BUILD_LEISURE_SMALL",
        "BUILD_FURN_SMALL",
        "BUILD_PATHS_SMALL",
        "BUILD_PLANTS_SMALL",
        "BUILD_ORNAMENTS_SMALL",
        "BUILD_TREES_SMALL",
        "BUILD_SHRUBS_SMALL",
        "BUILD_BILLBOARD_SMALL",
};
inline constexpr const char* kGuiImageListThumbNames[] = {
        "UI3_bp_thumb",
        "UI3_bp_thumb_grey",
        "UI3_sc_thumb_furn",
        "UI3_sc_thumb_path",
        "UI3_sc_thumb_grass",
        "UI3_sc_thumb_flower",
        "UI3_sc_thumb_orn",
        "UI3_sc_thumb_tree",
        "UI3_sc_thumb_plant",
        "UI3_sc_thumb_bill",
};
inline constexpr char kGuiBuildTipFormat[] = "BUILD_TIPTEXT_%s";
inline constexpr char kGuiSceneryTipFormat[] = "SP_DESC_%s";

} // namespace obl::gui_data
