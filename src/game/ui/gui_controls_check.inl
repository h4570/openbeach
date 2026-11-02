// Test drivers of gui_controls.cpp.
void Check(test::CallCounter& f, bool match, const std::string& detail)
{
    f.Record(match, detail);
    if (!match) OBL_LOG("autotest: FAILED %s %s", f.Name(), detail.c_str());
}
test::CallCounter g_seaOn("GuiControlsSeaOn"), g_seaOff("GuiControlsSeaOff");
test::CallCounter g_listOn("GuiControlsListOn"), g_listOff("GuiControlsListOff");
test::CallCounter g_musicPanel("GuiControlsMusicPanel");
void OpenMusicPanel(int count)
{
    if (count <= 0) return;
    const auto action = FindAction("MP3List");
    if (action) action(0);
    Check(g_musicPanel, action != nullptr, "open playlist through its action so the music player binds its list widget");
}
template<bool Playlist, bool On> void InspectSettings(int count)
{
    auto& check = Playlist ? (On ? g_listOn : g_listOff) : (On ? g_seaOn : g_seaOff);
    const auto& settings = game::settings_store::Current();
    for (int n = 0; n < count; ++n)
        Check(check, (Playlist ? settings.m_playlistMode : settings.m_seaEffects) == int(On),
                Playlist ? "real radio group writes playlist mode" : "real checkbox writes sea effects");
}

// Real clicks on the InGameOptions volume sliders (tests/gui_range.txt): a click in the middle of the 168 px travel stores 87/168 cut to hundredths.
test::CallCounter g_volumeMusic("GuiRangeMusicVolume"), g_volumeEffects("GuiRangeEffectsVolume");
template<bool Music> void InspectVolume(int count)
{
    auto& check = Music ? g_volumeMusic : g_volumeEffects;
    const auto& settings = game::settings_store::Current();
    const float value = Music ? settings.m_musicVolume : settings.m_effectsVolume;
    for (int n = 0; n < count; ++n)
        Check(check, std::fabs(value - 0.51f) < 1e-6f, "slider click stores the quantized volume " + std::to_string(value));
}
