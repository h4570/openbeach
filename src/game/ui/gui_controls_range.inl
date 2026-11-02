#include "core/char16.h"

using Scroll = ui::GuiScrollBar;
using SliderW = ui::GuiSlider;
using Adjust = ui::GuiNumberAdjust;

// ---- calls into the game's weather, sound and input objects -------------------------------------------------------------------------
double (*const kWeatherGet[3])(weather::Weather*) = {
        weather::runtime::GetOverrideRed, weather::runtime::GetOverrideGreen, weather::runtime::GetOverrideBlue};
void (*const kWeatherSet[3])(weather::Weather*, float) = {
        weather::runtime::SetOverrideRed, weather::runtime::SetOverrideGreen, weather::runtime::SetOverrideBlue};
template<int N> double WeatherGet(void* target, void*)
{
    return kWeatherGet[N](static_cast<weather::Weather*>(target));
}
template<int N> void WeatherSet(void* target, void*, float value)
{
    kWeatherSet[N](static_cast<weather::Weather*>(target), value);
}
float SoundGetEffects(void* target, void*)
{
    return (static_cast<audio::Sound*>(target))->GetEffectsVolume();
}
void SoundSetMusic(void* target, void*, float value)
{
    (static_cast<audio::Sound*>(target))->SetMusicVolume(value);
}
void SoundSetEffects(void* target, void*, float value)
{
    (static_cast<audio::Sound*>(target))->SetEffectsVolume(value);
}
int MouseDelta(void* input, void*)
{
    return static_cast<ui::Input*>(input)->m_deltaY;
}

// ---- shared helpers -----------------------------------------------------------------------------------------------------------------
void ProviderText(void* provider, const char* key, obl::Char16* out)
{
    provider_virtual::GetVariableText(provider, key, out);
}
void ProviderStore(void* provider, const char* key, float value)
{
    const uint32_t id = provider ? static_cast<object::Base*>(provider)->m_id : 0xffffffffu;
    if (!std::strncmp(key, "STAFF_WAGES", 11))
        game::SubmitCommand(command::SetStaffWage{id, FloatToIntWrap(value)});
    else if (!std::strncmp(key, "COMMOD_PRICE_", 13))
        game::SubmitCommand(command::SetPrice{id, std::atoi(key + 13), value});
    else if (!std::strncmp(key, "EVENT", 5) && std::strlen(key) == 12 && !std::strcmp(key + 6, "_PRICE"))
        game::SubmitCommand(command::SetEventPrice{id, key[5] - '0', value});
    else if (const auto control = command::MakeObjectControl(id, key, value))
        std::visit([](const auto& typed) { game::SubmitCommand(typed); }, *control);
}
void* SoundObject()
{
    return audio::CurrentSound();
}
Base* NewButton(int x0, int y0, int x1, int y1, const char* graphic)
{
    void* memory = Allocate(sizeof(Button));
    if (!memory) return nullptr;
    return static_cast<Base*>(button_facade::Construct(static_cast<Button*>(memory), x0, y0, x1, y1, graphic, "None", "NONE", "NONE"));
}
Base* NewMainItem(int x0, int y0, int x1, int y1, int variant)
{
    void* memory = Allocate(sizeof(ui::GuiMainItem));
    if (!memory) return nullptr;
    return static_cast<Base*>(container::ConstructMainItem(static_cast<ui::GuiMainItem*>(memory), x0, y0, x1, y1, variant));
}
Base* NewImage(int x0, int y0, int x1, int y1, const char* sprite)
{
    void* memory = Allocate(sizeof(ui::GuiImage));
    if (!memory) return nullptr;
    return static_cast<Base*>(ImageConstruct(static_cast<ui::GuiImage*>(memory), x0, y0, x1, y1, sprite));
}
void SetFrame(Base* image, int frame)
{
    GuiImageSetFrame(reinterpret_cast<ui::GuiImage*>(image), frame);
}
void ForwardUpdate(Base* w, float dt)
{
    EmptyCallFunction(w, std::bit_cast<int>(dt));
}
void DeleteChild(Base* child)
{
    if (child) gui::DestroyWidget(*child);
}

struct RangeEnv : ControlEnv {
    ui::GuiButtonData* NewButton(int x0, int y0, int x1, int y1, const char* graphic)
    {
        return static_cast<Button*>(gui::NewButton(x0, y0, x1, y1, graphic));
    }
    Base* NewMainItem(int x0, int y0, int x1, int y1, int variant) { return gui::NewMainItem(x0, y0, x1, y1, variant); }
    Base* NewImage(int x0, int y0, int x1, int y1, const char* sprite) { return gui::NewImage(x0, y0, x1, y1, sprite); }
    void SetFrame(Base* image, int frame) { gui::SetFrame(image, frame); }
    void SetVisibleBase(Base& widget, uint32_t visible) { widget.SetVisibleBase(visible); }
    void SetVisible(Base& widget, uint32_t visible) { widget.SetVisible(visible); }
    void SetTooltip(Base& widget, const char* text) { widget.SetTooltip(text); }
};

// ---- GuiScrollBar -------------------------------------------------------------------------------------------------------------------
void ScrollPlaceThumb(Scroll* w)
{
    gui_widgets::SetPosition(*w->m_thumb, w->m_thumb->m_x0, gui_controls::ScrollThumbY(*w));
}
void RunScrollSetCount(Scroll* w, uint32_t count)
{
    if (!gui_controls::ScrollSetCount(*w, count)) return;
    if (!w->m_thumb) return;
    container::MainItemSetHeight(static_cast<ui::GuiMainItem*>(w->m_thumb), w->m_thumbHeight);
    ScrollPlaceThumb(w);
}
void ScrollInit(Scroll* w, uint32_t top, uint32_t count, uint32_t rows, uint32_t id)
{
    RangeEnv env;
    gui_controls::ConstructScrollBar(*w, top, count, rows, id, env);
}
void ScrollDestruct(Scroll* w)
{
    w->SetVisibleBase(0);
    w->m_up->SetVisible(0u);
    w->m_down->SetVisible(0u);
    DeleteChild(w->m_up);
    DeleteChild(w->m_down);
    DeleteChild(w->m_thumb);
    DeleteChild(w->m_track);
}
void ScrollVisible(Scroll* w, uint32_t visible)
{
    w->SetVisibleBase(visible);
    w->m_up->SetVisible(visible);
    w->m_down->SetVisible(visible);
}
bool ScrollWheel(Scroll* w, int, int, int delta)
{
    if (delta > 0) {
        if (w->m_top == 0) return true;
        --w->m_top;
    } else if (delta < 0) {
        if (!(w->m_top < w->m_count - w->m_visibleRows)) return true;
        ++w->m_top;
    } else
        return true;
    ScrollPlaceThumb(w);
    return true;
}
void ScrollPage(Scroll* w, bool down)
{
    if (w->m_visibleRows - 1 == 0) return;
    for (uint32_t n = 0; n < w->m_visibleRows - 1; ++n) {
        if (down ? w->m_top < w->m_count - w->m_visibleRows : w->m_top != 0) {
            w->m_top += down ? 1u : static_cast<uint32_t>(-1);
            ScrollPlaceThumb(w);
        }
    }
}
bool ScrollMouse(Scroll* w, int x, int y, int state)
{
    w->m_hovered = w->HitTest(x, y);
    if (state != 2) w->m_dragging = 0;
    bool handled = w->m_up->MouseLeft(x, y, state);
    if (!handled) {
        handled = w->m_down->MouseLeft(x, y, state);
        if (!handled) {
            w->m_thumb->MouseLeft(x, y, state);
            if (w->m_thumb->IsHeld())
                w->m_dragging = 1;
            else if (w->m_thumb->WasClicked()) {
                w->m_thumb->m_clicked = 0;
                handled = true;
            }
        }
    }
    if (w->m_up->IsHeld() || w->m_down->IsHeld()) handled = true;
    if (w->m_up->IsHovered() || w->m_down->IsHovered()) handled = true;
    if (!w->m_dragging) {
        if (!handled && !w->m_thumb->HitTest(x, y)) {
            w->m_track->MouseLeft(x, y, state);
            if (w->m_track->WasClicked() || w->m_track->IsHeld()) {
                w->m_track->m_clicked = 0;
                handled = true;
                ScrollPage(w, y > w->m_thumb->m_y0);
            }
        }
    }
    if (w->m_dragging) {
        const int delta = gui_controls::ScrollClampDelta(
                MouseDelta(game::GetInput(), nullptr), w->m_thumb->m_y0, w->m_thumb->m_y1, w->m_up->m_y1, w->m_down->m_y0);
        w->m_thumb->Translate(0, delta);
        w->m_top = gui_controls::ScrollDragTop(*w, w->m_thumb->m_y0, w->m_up->m_y1);
        handled = true;
    }
    return w->IsHovered() && state == 2 ? true : handled;
}
void ScrollUpdate(Scroll* w, float dt)
{
    ForwardUpdate(w, dt);
    w->m_up->Update(dt);
    w->m_down->Update(dt);
    if (w->m_down->WasClicked()) {
        if (w->m_top < w->m_count - w->m_visibleRows) {
            ++w->m_top;
            ScrollPlaceThumb(w);
        }
        w->m_down->m_clicked = 0;
    }
    if (w->m_up->WasClicked()) {
        if (w->m_top != 0) {
            --w->m_top;
            ScrollPlaceThumb(w);
        }
        w->m_up->m_clicked = 0;
    }
}
void ScrollTopRow(Scroll* w, uint32_t top)
{
    const uint32_t limit = w->m_count - w->m_visibleRows;
    w->m_top = top > limit ? limit : top;
    ScrollPlaceThumb(w);
}
void ScrollTranslate(Scroll* w, int dx, int dy)
{
    w->TranslateBase(dx, dy);
    for (Base* child : {w->m_up, w->m_down, w->m_thumb, w->m_track}) child->Translate(dx, dy);
}
void ScrollOverlay(Scroll* w)
{
    for (Base* child : {w->m_track, w->m_up, w->m_down, w->m_thumb}) {
        child->Invalidate();
        child->Draw();
    }
}

// ---- GuiSlider ----------------------------------------------------------------------------------------------------------------------
enum class SliderKey { Red, Green, Blue, Music, Effects, Other };
SliderKey KeyOf(const char* name)
{
    if (std::strcmp(name, "WEATHER_RED") == 0) return SliderKey::Red;
    if (std::strcmp(name, "WEATHER_GREEN") == 0) return SliderKey::Green;
    if (std::strcmp(name, "WEATHER_BLUE") == 0) return SliderKey::Blue;
    if (std::strcmp(name, "MUSIC_VOLUME") == 0) return SliderKey::Music;
    if (std::strcmp(name, "EFFECTS_VOLUME") == 0) return SliderKey::Effects;
    return SliderKey::Other;
}
void* WeatherObject()
{
    return game::GetWeather();
}
void SliderInit(SliderW* w, const char* variable, uint32_t param0, uint32_t param1, int frame)
{
    RangeEnv env;
    gui_controls::ConstructSlider(*w, variable, param0, param1, frame, env);
}
void SliderDestruct(SliderW* w)
{
    DeleteChild(w->m_knob);
    Delete(w->m_variable);
    DeleteChild(w->m_track);
}
void SliderSet(ui::GuiSliderData* w, float value)
{
    const float quantized = gui_controls::SliderQuantize(value);
    switch (KeyOf(w->m_variable)) {
    case SliderKey::Red: WeatherSet<0>(WeatherObject(), nullptr, quantized); return;
    case SliderKey::Green: WeatherSet<1>(WeatherObject(), nullptr, quantized); return;
    case SliderKey::Blue: WeatherSet<2>(WeatherObject(), nullptr, quantized); return;
    case SliderKey::Music:
        game::settings_store::Current().m_musicVolume = quantized;
        SoundSetMusic(SoundObject(), nullptr, quantized);
        return;
    case SliderKey::Effects:
        game::settings_store::Current().m_effectsVolume = quantized;
        SoundSetEffects(SoundObject(), nullptr, quantized);
        return;
    case SliderKey::Other: ProviderStore(Provider(), w->m_variable, quantized); return;
    }
}
bool SliderMouse(SliderW* w, int x, int y, int state)
{
    w->m_hovered = w->HitTest(x, y);
    Track(w, nullptr, x, y, state);
    w->m_knob->MouseLeft(x, y, state);
    if (w->IsHovered() && state == 1)
        w->m_dragging = static_cast<uint8_t>(state);
    else if (!w->IsHovered() || state == 0)
        w->m_dragging = 0;
    if (!w->m_dragging || state == 3) return false;
    x = gui_controls::SliderClampX(x, w->m_x0, w->m_x1);
    w->m_knob->Translate(gui_widgets::Sub(x, w->m_knob->m_x0), 0);
    SliderSet(w, gui_controls::SliderValue(w->m_knob->m_x0, w->m_x0, w->m_range));
    return true;
}
void SliderOrigin(SliderW* w, int dx, int dy)
{
    w->SetOriginBase(dx, dy);
    w->m_knob->SetOrigin(dx, dy);
    w->m_track->SetOrigin(dx, dy);
}
void SliderTranslate(SliderW* w, int dx, int dy)
{
    w->TranslateBase(dx, dy);
    w->m_knob->Translate(dx, dy);
    w->m_track->Translate(dx, dy);
}
void SliderOverlay(SliderW* w)
{
    for (Base* child : {w->m_track, w->m_knob}) {
        child->Invalidate();
        child->Draw();
    }
}
void SliderUpdate(SliderW* w, float dt)
{
    ForwardUpdate(w, dt);
    float value;
    switch (KeyOf(w->m_variable)) {
    case SliderKey::Red: value = static_cast<float>(WeatherGet<0>(WeatherObject(), nullptr)); break;
    case SliderKey::Green: value = static_cast<float>(WeatherGet<1>(WeatherObject(), nullptr)); break;
    case SliderKey::Blue: value = static_cast<float>(WeatherGet<2>(WeatherObject(), nullptr)); break;
    case SliderKey::Music: value = audio::CurrentSound()->musicVolume; break;
    case SliderKey::Effects: value = SoundGetEffects(SoundObject(), nullptr); break;
    default: {
        obl::Char16 text[512];
        obl::Char16* end;
        ProviderText(Provider(), w->m_variable, text);
        value = static_cast<float>(::obl::ui::host_text::ParseNumber(text, &end));
    }
    }
    w->m_value = value;
    const int x = gui_controls::SliderKnobX(*w, value);
    gui_widgets::SetPosition(*w->m_knob, x, w->m_knob->m_y0);
}

// ---- GuiNumberAdjust ----------------------------------------------------------------------------------------------------------------
void AdjustInit(Adjust* w, int kind, int font, const char* variable, uint8_t wrap, uint8_t tips)
{
    RangeEnv env;
    gui_controls::ConstructNumberAdjust(*w, kind, font, variable, wrap, tips, env);
}
void AdjustDestruct(Adjust* w)
{
    DeleteChild(w->m_inc);
    DeleteChild(w->m_dec);
    DeleteChild(w->m_left);
    DeleteChild(w->m_right);
    Delete(w->m_variable);
}
void AdjustTooltip(Adjust* w, const char* text)
{
    w->m_left->SetTooltip(text);
    if (w->m_right) w->m_right->SetTooltip(text);
}
void AdjustVisible(Adjust* w, uint32_t visible)
{
    w->SetVisibleBase(visible);
    w->m_left->SetVisible(visible);
    if (w->m_right) w->m_right->SetVisible(visible);
    w->m_inc->SetVisible(visible);
    w->m_dec->SetVisible(visible);
}
void* AdjustProvider(const ui::GuiNumberAdjustData* w)
{
    return w->m_provider ? w->m_provider : Provider();
}
void AdjustPress(ui::GuiNumberAdjustData* w, Base* button, int dir)
{
    const float next = gui_controls::AdjustStep(w->m_value, w->m_part, w->m_wrap != 0, dir);
    void* provider = AdjustProvider(w);
    if (w->m_kind == 0) {
        const int whole = FloatToIntWrap(next);
        ProviderStore(provider, w->m_variable, static_cast<float>(whole));
    } else if (w->m_kind == 1)
        ProviderStore(provider, w->m_variable, next);
    button->m_clicked = 0;
}

template<int kind> void DriveCommandControl(int id)
{
    auto* object = game::GetWorld() ? object::object_manager::ObjectById(game::GetObjectManager(), static_cast<uint32_t>(id)) : nullptr;
    if (!object) {
        OBL_LOG("autotest: FAILED GuiControl command driver needs gameplay and an object id");
        return;
    }
    auto* manager = game::GetGuiManager();
    void* saved = manager->m_variableProvider;
    manager->m_variableProvider = object;
    if constexpr (kind == 0 || kind == 5) {
        ui::GuiNumberAdjustData adjust{};
        Base button{};
        adjust.m_provider = object;
        adjust.m_variable = const_cast<char*>(kind == 0 ? "STAFF_WAGES" : "BUILDING_REPAIRINTERVAL");
        adjust.m_value = kind == 0 ? 39.0f : 6.0f;
        AdjustPress(&adjust, &button, 1);
    } else if constexpr (kind == 3) {
        Write(object, "CLEANER_LITTER", 0);
    } else {
        ui::GuiSliderData slider{};
        slider.m_variable = const_cast<char*>(kind == 1 ? "COMMOD_PRICE_0"
                        : kind == 2                     ? "EVENT1_PRICE"
                        : kind == 6                     ? "WEATHER_RED"
                                                        : "BEER_STRENGTH");
        SliderSet(&slider, 0.375f);
    }
    manager->m_variableProvider = saved;
}
test::CallCounter g_wageCommand("GuiControlWage");
test::CallCounter g_priceCommand("GuiControlPrice");
test::CallCounter g_eventPriceCommand("GuiControlEventPrice");
test::CallCounter g_toggleCommand("GuiControlToggle");
test::CallCounter g_variableCommand("GuiControlVariable");
test::CallCounter g_intervalCommand("GuiControlInterval");
test::CallCounter g_speedCommand("GuiControlSpeed");
test::CallCounter g_weatherSlider("GuiControlWeather");
void DriveCommandSpeed(int level)
{
    GameSet(game::GetGame(), nullptr, "GAME_GAME_SPEED", &level);
}
void AdjustUpdate(Adjust* w, float dt)
{
    w->m_inc->Update(dt);
    w->m_dec->Update(dt);
    obl::Char16 text[260];
    obl::Char16* end;
    ProviderText(AdjustProvider(w), w->m_variable, text);
    w->m_value = static_cast<float>(::obl::ui::host_text::ParseNumber(text, &end));
    if (w->m_inc->WasClicked()) AdjustPress(w, w->m_inc, 1);
    if (w->m_dec->WasClicked()) AdjustPress(w, w->m_dec, -1);
}
bool AdjustMouse(Adjust* w, int x, int y, int state)
{
    if (w->m_right) {
        w->m_right->MouseLeft(x, y, state);
        if (w->m_right->WasClicked()) {
            w->m_right->m_clicked = 0;
            w->m_part = 1;
            SetFrame(w->m_left, 0);
            if (w->m_right) SetFrame(w->m_right, 1);
        } else {
            w->m_left->MouseLeft(x, y, state);
            if (w->m_left->WasClicked()) {
                w->m_left->m_clicked = 0;
                w->m_part = 0;
                SetFrame(w->m_left, 1);
                if (w->m_right) SetFrame(w->m_right, 0);
            }
        }
    } else
        w->m_left->MouseLeft(x, y, state);
    bool handled = w->m_inc->MouseLeft(x, y, state);
    if (!handled) handled = w->m_dec->MouseLeft(x, y, state);
    return handled;
}
void AdjustDraw(Adjust* w)
{
    w->m_left->Draw();
    if (w->m_right) w->m_right->Draw();
    w->m_inc->Draw();
    w->m_dec->Draw();
}
void AdjustInvalidate(Adjust* w)
{
    w->m_left->Invalidate();
    if (w->m_right) w->m_right->Invalidate();
    w->m_inc->Invalidate();
    w->m_dec->Invalidate();
}
void AdjustTranslate(Adjust* w, int dx, int dy)
{
    w->TranslateBase(dx, dy);
    w->m_left->Translate(dx, dy);
    if (w->m_right) w->m_right->Translate(dx, dy);
    w->m_inc->Translate(dx, dy);
    w->m_dec->Translate(dx, dy);
}
void RunAdjustOrigin(Adjust* w, int dx, int dy)
{
    w->SetOriginBase(dx, dy);
    w->m_left->SetOrigin(dx, dy);
    if (w->m_right) w->m_right->SetOrigin(dx, dy);
    w->m_inc->SetOrigin(dx, dy);
    w->m_dec->SetOrigin(dx, dy);
}
void AdjustOverlay(Adjust* w, gui_canvas::Canvas& canvas)
{
    const int font = w->m_font;
    w->m_left->DrawOverlay();
    if (w->m_right) w->m_right->DrawOverlay();
    w->m_inc->DrawOverlay();
    w->m_dec->DrawOverlay();
    canvas.SetTextAlign(font, 1);
    canvas.SetTextOutlined(font, false);
    double whole;
    const double fraction = std::modf(static_cast<double>(w->m_value), &whole);
    const float wholeF = static_cast<float>(whole), fractionF = static_cast<float>(fraction);
    canvas.SetTextColor(font, w->m_part == 0 ? 0xffffffffu : 0u);
    const auto left = gui_widgets::GetBounds(*w->m_left);
    canvas.SetTextRect(font, left.x0, left.y0, 0x17, 0xd);
    const auto print = ::obl::ui::host_text::Print;
    obl::Char16 text[32];
    print(text, u"%d", FloatToIntWrap(wholeF));
    canvas.DrawWrapped(font, 0, 0, text);
    if (!w->m_right) return;
    canvas.SetTextColor(font, 0);
    canvas.SetTextRect(font, gui_widgets::Add(left.x0, 9), gui_widgets::Add(left.y0, 2), 0x17, 0xd);
    print(text, u".");
    canvas.DrawWrapped(font, 0, 0, text);
    canvas.SetTextColor(font, w->m_part == 1 ? 0xffffffffu : 0u);
    const auto right = gui_widgets::GetBounds(*w->m_right);
    canvas.SetTextRect(font, right.x0, right.y0, 0xf, 0xd);
    print(text, u"%02.0f", static_cast<double>(fractionF) * 100.0);
    canvas.DrawWrapped(font, 0, 0, text);
}
void LiveAdjustOverlay(Adjust* w)
{
    LiveCanvas c;
    AdjustOverlay(w, c);
}

// ---- entry points ------------------------------------------------------------------------------------------------------------------------
} // namespace

namespace {
