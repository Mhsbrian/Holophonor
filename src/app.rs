use crate::visuals::{ArtMode, Canvas};
use eframe::egui::{self, Align2, Color32, FontFamily, FontId, Pos2, Rect, Stroke, Vec2};
use holophonor::{
    engine::{AudioEngine, MidiWorker, Shared},
    midi::{Performance, Profile},
    synth::Voice,
};
use serde::{Deserialize, Serialize};
use std::{
    sync::{Arc, atomic::Ordering},
    time::{Duration, Instant},
};

const PAPER: Color32 = Color32::from_rgb(246, 243, 235);
const INK: Color32 = Color32::from_rgb(57, 64, 60);
const MUTED: Color32 = Color32::from_rgb(115, 121, 110);
const LINE: Color32 = Color32::from_rgb(218, 216, 204);
const ACCENT: Color32 = Color32::from_rgb(100, 124, 106);

#[derive(Serialize, Deserialize)]
#[serde(default)]
struct Preferences {
    voice: Voice,
    mode: ArtMode,
    profile: Profile,
    gain: f32,
    bend_range: f32,
    sensitivity: f32,
    port: Option<String>,
}

impl Default for Preferences {
    fn default() -> Self {
        Self {
            voice: Voice::Reed,
            mode: ArtMode::Ink,
            profile: Profile::default(),
            gain: 0.5,
            bend_range: 2.0,
            sensitivity: 1.0,
            port: None,
        }
    }
}

pub struct Holophonor {
    preferences: Preferences,
    shared: Arc<Shared>,
    _midi: MidiWorker,
    audio: Option<AudioEngine>,
    paper: egui::TextureHandle,
    canvas: Canvas,
    instrument_menu: bool,
    visual_menu: bool,
    settings_open: bool,
    help_open: bool,
    search: String,
    started: Instant,
    last_frame: Instant,
    preview_started: Instant,
    frames: u32,
    capture: Option<String>,
    capture_requested: bool,
    capture_error: Option<String>,
}

fn serif(size: f32) -> FontId {
    FontId::new(size, FontFamily::Name("serif".into()))
}
fn italic(size: f32) -> FontId {
    FontId::new(size, FontFamily::Name("italic".into()))
}
fn sans(size: f32) -> FontId {
    FontId::proportional(size)
}

impl Holophonor {
    pub fn new(
        cc: &eframe::CreationContext<'_>,
        demo: bool,
        silent: bool,
        capture: Option<String>,
        settings: bool,
    ) -> Self {
        let mut fonts = egui::FontDefinitions::default();
        fonts.font_data.insert(
            "serif".into(),
            Arc::new(egui::FontData::from_static(include_bytes!(
                "../assets/fonts/LiberationSerif-Regular.ttf"
            ))),
        );
        fonts.font_data.insert(
            "italic".into(),
            Arc::new(egui::FontData::from_static(include_bytes!(
                "../assets/fonts/LiberationSerif-Italic.ttf"
            ))),
        );
        fonts
            .families
            .insert(FontFamily::Name("serif".into()), vec!["serif".into()]);
        fonts
            .families
            .insert(FontFamily::Name("italic".into()), vec!["italic".into()]);
        cc.egui_ctx.set_fonts(fonts);
        let mut style = egui::Style {
            visuals: egui::Visuals::light(),
            ..Default::default()
        };
        style.visuals.override_text_color = Some(INK);
        style.visuals.panel_fill = PAPER;
        style.visuals.window_fill = Color32::from_rgb(251, 249, 244);
        style.visuals.window_stroke = Stroke::new(1.0, LINE);
        style.visuals.window_corner_radius = 14.0.into();
        style.visuals.widgets.inactive.bg_fill = Color32::from_rgb(237, 236, 227);
        style.visuals.widgets.inactive.weak_bg_fill = Color32::from_rgb(240, 239, 231);
        style.visuals.widgets.inactive.bg_stroke = Stroke::new(0.7, LINE);
        style.visuals.widgets.hovered.bg_fill = Color32::from_rgb(229, 235, 224);
        style.visuals.widgets.hovered.weak_bg_fill = Color32::from_rgb(229, 235, 224);
        style.visuals.widgets.hovered.bg_stroke = Stroke::new(1.0, ACCENT);
        style.visuals.widgets.active.bg_fill = Color32::from_rgb(219, 231, 215);
        style.visuals.widgets.active.weak_bg_fill = Color32::from_rgb(219, 231, 215);
        style.visuals.selection.bg_fill = Color32::from_rgb(213, 225, 210);
        style.visuals.selection.stroke = Stroke::new(1.0, INK);
        style.spacing.item_spacing = Vec2::new(12.0, 12.0);
        style.spacing.button_padding = Vec2::new(12.0, 8.0);
        cc.egui_ctx.set_theme(egui::Theme::Light);
        cc.egui_ctx.set_global_style(style);
        let mut preferences: Preferences = cc
            .storage
            .and_then(|s| eframe::get_value(s, "holophonor.preferences"))
            .unwrap_or_default();
        preferences.gain = preferences.gain.clamp(0.0, 0.8);
        preferences.bend_range = preferences.bend_range.clamp(0.0, 24.0);
        preferences.sensitivity = preferences.sensitivity.clamp(0.25, 2.0);
        preferences.profile = Profile::unpack(preferences.profile.pack());
        let shared = Arc::new(Shared::default());
        shared
            .profile
            .store(preferences.profile.pack(), Ordering::Relaxed);
        shared
            .voice
            .store(preferences.voice as u32, Ordering::Relaxed);
        shared
            .gain
            .store(preferences.gain.to_bits(), Ordering::Relaxed);
        shared
            .bend_range
            .store(preferences.bend_range.to_bits(), Ordering::Relaxed);
        *shared.requested_port.lock().unwrap() = preferences.port.clone();
        let midi = MidiWorker::start(shared.clone());
        let audio = (!silent).then(|| AudioEngine::new(shared.clone()));
        let paper = make_paper(&cc.egui_ctx);
        let now = Instant::now();
        let mut app = Self {
            preferences,
            shared,
            _midi: midi,
            audio,
            paper,
            canvas: Canvas::default(),
            instrument_menu: false,
            visual_menu: false,
            settings_open: settings,
            help_open: false,
            search: String::new(),
            started: now,
            last_frame: now,
            preview_started: now,
            frames: 0,
            capture,
            capture_requested: false,
            capture_error: None,
        };
        if demo {
            app.start_preview();
            // Captures show a phrase already painted rather than depending on
            // wall-clock rendering speed. This never fabricates a device status.
            if app.capture.is_some() {
                for frame in 0..620 {
                    let state = preview(frame as f32 / 60.0);
                    app.canvas
                        .update(state, 1.0 / 60.0, app.preferences.sensitivity);
                }
            }
        }
        app
    }

    fn start_preview(&mut self) {
        self.preview_started = Instant::now();
        self.shared
            .preview
            .store(preview(0.0).pack(), Ordering::Relaxed);
        self.shared
            .resume(self.shared.panic_epoch.load(Ordering::SeqCst));
        self.shared.preview_on.store(true, Ordering::Relaxed);
    }

    fn toggle_preview(&mut self) {
        if self.shared.preview_on.load(Ordering::Relaxed) {
            self.shared.preview_on.store(false, Ordering::Relaxed);
        } else {
            self.start_preview();
        }
    }

    fn draw(&mut self, ui: &mut egui::Ui, ctx: &egui::Context) {
        let rect = ui.max_rect();
        let painter = ui.painter().clone();
        painter.rect_filled(rect, 0.0, PAPER);
        painter.image(
            self.paper.id(),
            rect,
            Rect::from_min_max(
                Pos2::ZERO,
                Pos2::new(rect.width() / 384.0, rect.height() / 384.0),
            ),
            Color32::WHITE,
        );
        let margin = if rect.width() < 1000.0 { 34.0 } else { 54.0 };
        let left = rect.left() + margin;
        let right = rect.right() - margin;
        let top = rect.top() + 36.0;
        let compact = rect.width() < 1000.0;
        let selector_width = if compact { 208.0 } else { 254.0 };
        let instrument_rect =
            Rect::from_min_size(Pos2::new(left, top), Vec2::new(selector_width, 72.0));
        if selector(
            ui,
            instrument_rect,
            "instrument",
            "INSTRUMENT",
            self.preferences.voice.name(),
            false,
        )
        .clicked()
        {
            self.instrument_menu = !self.instrument_menu;
            self.visual_menu = false;
            self.search.clear();
        }
        let art_rect = Rect::from_min_size(
            Pos2::new(right - selector_width, top),
            Vec2::new(selector_width, 72.0),
        );
        if selector(
            ui,
            art_rect,
            "visual",
            "VISUAL EXPRESSION",
            self.preferences.mode.name(),
            true,
        )
        .clicked()
        {
            self.visual_menu = !self.visual_menu;
            self.instrument_menu = false;
        }
        let logo = Pos2::new(rect.center().x, top + 16.0);
        draw_emblem(&painter, logo, 17.0, ACCENT);
        painter.text(
            logo + Vec2::new(0.0, 30.0),
            Align2::CENTER_CENTER,
            "h o l o p h o n o r",
            sans(if compact { 13.0 } else { 15.0 }),
            INK,
        );
        let divider_y = top + 106.0;
        painter.line_segment(
            [Pos2::new(left, divider_y), Pos2::new(right, divider_y)],
            Stroke::new(0.7, LINE),
        );
        painter.text(
            Pos2::new(left, divider_y + 27.0),
            Align2::LEFT_CENTER,
            "THE LIVING CANVAS",
            sans(10.0),
            MUTED,
        );
        painter.text(
            Pos2::new(right, divider_y + 27.0),
            Align2::RIGHT_CENTER,
            if self.canvas.frozen {
                "A MOMENT, HELD"
            } else {
                "SOUND, MADE VISIBLE"
            },
            sans(10.0),
            MUTED,
        );

        let canvas_rect = Rect::from_min_max(
            Pos2::new(left + 26.0, divider_y + 52.0),
            Pos2::new(right - 26.0, rect.bottom() - 174.0),
        );
        self.canvas
            .paint(&painter, canvas_rect, self.preferences.mode);
        if self.canvas.empty() {
            let center = canvas_rect.center() + Vec2::new(0.0, -20.0);
            draw_emblem(
                &painter,
                center + Vec2::new(0.0, -105.0),
                30.0,
                Color32::from_rgb(173, 182, 159),
            );
            painter.text(
                center + Vec2::new(0.0, -17.0),
                Align2::CENTER_CENTER,
                "Your breath,",
                serif(if compact { 46.0 } else { 58.0 }),
                INK,
            );
            painter.text(
                center + Vec2::new(0.0, 43.0),
                Align2::CENTER_CENTER,
                "made visible.",
                italic(if compact { 46.0 } else { 58.0 }),
                ACCENT,
            );
            let connected = self.shared.midi_status.lock().unwrap().connected.is_some();
            painter.text(
                center + Vec2::new(0.0, 105.0),
                Align2::CENTER_CENTER,
                if connected {
                    "Your instrument is here. Let the first note find its way."
                } else {
                    "Connect your wind instrument. The rest begins with you."
                },
                sans(14.0),
                MUTED,
            );
            let try_rect =
                Rect::from_center_size(center + Vec2::new(0.0, 151.0), Vec2::new(184.0, 38.0));
            if small_button(ui, try_rect, "try_preview", "Try a little melody").clicked() {
                self.toggle_preview();
            }
        }

        let footer_y = rect.bottom() - 116.0;
        painter.line_segment(
            [Pos2::new(left, footer_y), Pos2::new(right, footer_y)],
            Stroke::new(0.7, LINE),
        );
        let device = self.shared.midi_status.lock().unwrap().clone();
        let status_y = footer_y + 38.0;
        let device_rect = Rect::from_min_size(
            Pos2::new(left, status_y - 17.0),
            Vec2::new(if compact { 230.0 } else { 290.0 }, 34.0),
        );
        let device_response =
            ui.interact(device_rect, ui.id().with("device"), egui::Sense::click());
        device_response.widget_info(|| {
            egui::WidgetInfo::labeled(egui::WidgetType::Button, true, "Connection settings")
        });
        let is_connected = device.connected.is_some();
        painter.circle_filled(
            Pos2::new(left + 5.0, status_y),
            3.5,
            if is_connected {
                ACCENT
            } else {
                Color32::from_rgb(174, 159, 124)
            },
        );
        let name = device.connected.as_deref().unwrap_or("Awaiting your EWI");
        let short: String = name.chars().take(if compact { 23 } else { 30 }).collect();
        painter.text(
            Pos2::new(left + 20.0, status_y),
            Align2::LEFT_CENTER,
            short,
            sans(13.0),
            INK,
        );
        if device_response.clicked() {
            self.settings_open = !self.settings_open;
        }
        device_response.on_hover_text("Connections, breath calibration, and audio");
        let ready = self.audio.as_ref().is_some_and(AudioEngine::ready);
        let audio_text = match self.audio.as_ref() {
            Some(audio) if ready => {
                format!("Audio ready · {} kHz", audio.sample_rate as f32 / 1000.0)
            }
            Some(_) => "Audio unavailable · open settings".into(),
            None => "Silent canvas".into(),
        };
        painter.text(
            Pos2::new(left + 20.0, status_y + 25.0),
            Align2::LEFT_CENTER,
            audio_text,
            sans(11.0),
            if ready {
                MUTED
            } else {
                Color32::from_rgb(146, 107, 81)
            },
        );

        let state = self.shared.performance();
        let breath_center = rect.center().x;
        if !compact {
            for bar in 0..24 {
                let x = breath_center - 57.5 + bar as f32 * 5.0;
                let height = 5.0 + ((bar as f32 / 23.0) * std::f32::consts::PI).sin() * 15.0;
                let active = state.gate && bar as f32 / 24.0 < state.breath;
                painter.rect_filled(
                    Rect::from_center_size(Pos2::new(x, status_y), Vec2::new(2.0, height)),
                    1.0,
                    if active { ACCENT } else { LINE },
                );
            }
            let label = if self.shared.preview_on.load(Ordering::Relaxed) {
                "PREVIEW MELODY"
            } else {
                "BREATH"
            };
            painter.text(
                Pos2::new(breath_center, status_y + 26.0),
                Align2::CENTER_CENTER,
                label,
                sans(9.0),
                MUTED,
            );
        }

        let clear_rect = Rect::from_min_size(
            Pos2::new(right - 120.0, status_y - 18.0),
            Vec2::new(120.0, 36.0),
        );
        if small_button(ui, clear_rect, "clear", "New canvas").clicked() {
            self.canvas.clear();
        }
        let freeze_rect = Rect::from_min_size(
            Pos2::new(right - 205.0, status_y - 18.0),
            Vec2::new(76.0, 36.0),
        );
        if small_button(
            ui,
            freeze_rect,
            "freeze",
            if self.canvas.frozen {
                "Resume"
            } else {
                "Freeze"
            },
        )
        .clicked()
        {
            self.canvas.frozen = !self.canvas.frozen;
        }
        let help_rect = Rect::from_min_size(
            Pos2::new(right - 26.0, status_y + 20.0),
            Vec2::new(26.0, 28.0),
        );
        if text_button(ui, help_rect, "help", "?").clicked() {
            self.help_open = !self.help_open;
        }
        painter.text(
            Pos2::new(rect.center().x, rect.bottom() - 24.0),
            Align2::CENTER_CENTER,
            "Space  ·  preview       Esc  ·  silence       F  ·  freeze       C  ·  new canvas",
            sans(10.0),
            MUTED,
        );

        if self.shared.is_muted() {
            painter.text(
                Pos2::new(rect.center().x, footer_y - 28.0),
                Align2::CENTER_CENTER,
                "Silenced. Start a fresh note, or press Space to preview.",
                sans(12.0),
                MUTED,
            );
        }
        self.instrument_popup(ctx, instrument_rect);
        self.visual_popup(ctx, art_rect);
    }

    fn instrument_popup(&mut self, ctx: &egui::Context, anchor: Rect) {
        if !self.instrument_menu {
            return;
        }
        let area = egui::Area::new("instrument_popup".into())
            .order(egui::Order::Foreground)
            .fixed_pos(anchor.left_bottom() + Vec2::new(0.0, 10.0))
            .show(ctx, |ui| {
                popup_frame().show(ui, |ui| {
                    ui.set_width(290.0);
                    ui.label(egui::RichText::new("Find your voice").font(serif(26.0)));
                    ui.add(
                        egui::TextEdit::singleline(&mut self.search)
                            .hint_text("Search instruments…")
                            .desired_width(290.0),
                    );
                    let search = self.search.to_lowercase();
                    let mut found = false;
                    for voice in Voice::ALL {
                        if !format!("{} {}", voice.name(), voice.family())
                            .to_lowercase()
                            .contains(&search)
                        {
                            continue;
                        }
                        found = true;
                        ui.label(egui::RichText::new(voice.family()).size(9.0).color(MUTED));
                        if ui
                            .add_sized(
                                [290.0, 38.0],
                                egui::Button::new(
                                    egui::RichText::new(voice.name()).font(serif(23.0)),
                                )
                                .selected(self.preferences.voice == voice),
                            )
                            .clicked()
                        {
                            self.preferences.voice = voice;
                            self.shared.voice.store(voice as u32, Ordering::Relaxed);
                            self.instrument_menu = false;
                        }
                        ui.label(
                            egui::RichText::new(voice.description())
                                .size(11.0)
                                .color(MUTED),
                        );
                    }
                    if !found {
                        ui.label("No voices found. Try “wind” or “reed”.");
                    }
                    ui.separator();
                    ui.label(
                        egui::RichText::new("Four original synthesis voices. A beginning.")
                            .size(10.0)
                            .color(MUTED),
                    );
                });
            });
        if ctx.input(|i| {
            i.pointer.any_pressed()
                && i.pointer
                    .interact_pos()
                    .is_some_and(|p| !area.response.rect.contains(p) && !anchor.contains(p))
        }) {
            self.instrument_menu = false;
        }
    }

    fn visual_popup(&mut self, ctx: &egui::Context, anchor: Rect) {
        if !self.visual_menu {
            return;
        }
        let area = egui::Area::new("visual_popup".into())
            .order(egui::Order::Foreground)
            .fixed_pos(anchor.right_bottom() + Vec2::new(-316.0, 10.0))
            .show(ctx, |ui| {
                popup_frame().show(ui, |ui| {
                    ui.set_width(276.0);
                    ui.label(egui::RichText::new("Let sound take shape").font(serif(25.0)));
                    for mode in ArtMode::ALL {
                        if ui
                            .add_sized(
                                [276.0, 38.0],
                                egui::Button::new(
                                    egui::RichText::new(mode.name()).font(serif(22.0)),
                                )
                                .selected(self.preferences.mode == mode),
                            )
                            .clicked()
                        {
                            self.preferences.mode = mode;
                            self.visual_menu = false;
                        }
                        ui.label(
                            egui::RichText::new(mode.description())
                                .size(11.0)
                                .color(MUTED),
                        );
                    }
                    ui.separator();
                    ui.label(egui::RichText::new("Motion").size(11.0).color(MUTED));
                    ui.add(
                        egui::Slider::new(&mut self.preferences.sensitivity, 0.25..=2.0)
                            .show_value(false)
                            .text("Still to expressive"),
                    );
                });
            });
        if ctx.input(|i| {
            i.pointer.any_pressed()
                && i.pointer
                    .interact_pos()
                    .is_some_and(|p| !area.response.rect.contains(p) && !anchor.contains(p))
        }) {
            self.visual_menu = false;
        }
    }

    fn settings(&mut self, ctx: &egui::Context) {
        let previous_profile = self.preferences.profile;
        let previous_port = self.preferences.port.clone();
        let mut open = self.settings_open;
        egui::Window::new("A few quiet adjustments")
            .open(&mut open)
            .collapsible(false)
            .resizable(false)
            .default_width(430.0)
            .max_height(ctx.content_rect().height() - 64.0)
            .vscroll(true)
            .anchor(Align2::CENTER_CENTER, Vec2::ZERO)
            .show(ctx, |ui| {
                let status = self.shared.midi_status.lock().unwrap().clone();
                ui.label(
                    egui::RichText::new("YOUR INSTRUMENT")
                        .size(10.0)
                        .color(MUTED),
                );
                egui::ComboBox::from_id_salt("port")
                    .selected_text(
                        self.preferences
                            .port
                            .as_deref()
                            .unwrap_or("Automatic discovery"),
                    )
                    .width(390.0)
                    .show_ui(ui, |ui| {
                        ui.selectable_value(
                            &mut self.preferences.port,
                            None,
                            "Automatic discovery",
                        );
                        for name in &status.ports {
                            ui.selectable_value(
                                &mut self.preferences.port,
                                Some(name.clone()),
                                name,
                            );
                        }
                    });
                ui.label(
                    egui::RichText::new(
                        status.connected.as_deref().unwrap_or(
                            "Plug in a USB MIDI wind controller. Listening automatically.",
                        ),
                    )
                    .size(12.0)
                    .color(MUTED),
                );
                if let Some(error) = status.error {
                    ui.colored_label(Color32::from_rgb(160, 90, 70), error);
                }
                ui.horizontal(|ui| {
                    if ui.button("Breath · CC2").clicked() {
                        self.preferences.profile = Profile::default();
                    }
                    if ui.button("Expression · CC11").clicked() {
                        self.preferences.profile = Profile {
                            breath_cc: 11,
                            ..Profile::default()
                        };
                    }
                    if ui.button("Keyboard").clicked() {
                        self.preferences.profile = Profile {
                            velocity_breath: true,
                            ..Profile::default()
                        };
                    }
                });
                ui.horizontal(|ui| {
                    ui.label("Breath CC");
                    ui.add(
                        egui::DragValue::new(&mut self.preferences.profile.breath_cc)
                            .range(0..=127),
                    );
                    ui.label("Channel");
                    ui.add(
                        egui::DragValue::new(&mut self.preferences.profile.channel).range(0..=16),
                    );
                    ui.label(egui::RichText::new("0 = all").size(10.0).color(MUTED));
                });
                ui.checkbox(
                    &mut self.preferences.profile.velocity_breath,
                    "Use note velocity when no breath controller is available",
                );
                ui.label(
                    egui::RichText::new("BREATH CALIBRATION")
                        .size(10.0)
                        .color(MUTED),
                );
                ui.add(
                    egui::Slider::new(&mut self.preferences.profile.floor, 0..=126)
                        .text("Resting breath"),
                );
                let floor = self.preferences.profile.floor;
                self.preferences.profile.ceiling = self.preferences.profile.ceiling.max(floor + 1);
                ui.add(
                    egui::Slider::new(&mut self.preferences.profile.ceiling, floor + 1..=127)
                        .text("Full breath"),
                );
                let state = self.shared.performance();
                ui.add(egui::ProgressBar::new(state.breath).text(format!(
                    "Breath {:.0}% · {} · {} messages",
                    state.breath * 100.0,
                    note_name(state.note),
                    self.shared.midi_messages.load(Ordering::Relaxed)
                )));
                ui.add(
                    egui::Slider::new(&mut self.preferences.bend_range, 0.0..=24.0)
                        .text("Pitch bend · semitones"),
                );
                ui.separator();
                ui.label(egui::RichText::new("AUDIO").size(10.0).color(MUTED));
                ui.add(
                    egui::Slider::new(&mut self.preferences.gain, 0.0..=0.8).text("Output level"),
                );
                if let Some(audio) = &mut self.audio {
                    ui.label(&audio.status);
                    ui.label(
                        egui::RichText::new(&audio.device_name)
                            .size(11.0)
                            .color(MUTED),
                    );
                    let frames = self.shared.buffer_frames.load(Ordering::Relaxed);
                    let milliseconds = frames as f32 / audio.sample_rate.max(1) as f32 * 1000.0;
                    ui.label(
                        egui::RichText::new(format!(
                            "{} Hz · {} frames · {:.1} ms per buffer · {} stream errors",
                            audio.sample_rate,
                            frames,
                            milliseconds,
                            self.shared.audio_errors.load(Ordering::Relaxed)
                        ))
                        .size(10.0)
                        .color(MUTED),
                    );
                    ui.label(
                        egui::RichText::new("Buffer duration excludes device and system latency.")
                            .size(10.0)
                            .color(MUTED),
                    );
                } else {
                    ui.label("Audio was disabled with --silent.");
                }
                ui.horizontal(|ui| {
                    if ui.button("Preview melody").clicked() {
                        self.toggle_preview();
                    }
                    if ui.button("Silence all · Esc").clicked() {
                        self.shared.panic();
                    }
                });
                ui.label(
                    egui::RichText::new(
                        "Preferences are remembered. USB MIDI needs no account or network.",
                    )
                    .size(10.0)
                    .color(MUTED),
                );
                if let Some(error) = &self.capture_error {
                    ui.colored_label(Color32::from_rgb(160, 90, 70), error);
                }
            });
        self.settings_open = open;
        if previous_profile != self.preferences.profile {
            self.shared.panic();
            self.shared
                .profile
                .store(self.preferences.profile.pack(), Ordering::Relaxed);
        }
        if previous_port != self.preferences.port {
            self.shared.panic();
            *self.shared.requested_port.lock().unwrap() = self.preferences.port.clone();
        }
        self.shared
            .gain
            .store(self.preferences.gain.to_bits(), Ordering::Relaxed);
        self.shared
            .bend_range
            .store(self.preferences.bend_range.to_bits(), Ordering::Relaxed);
    }

    fn help(&mut self, ctx: &egui::Context) {
        let mut open = self.help_open;
        egui::Window::new("A breath is all it takes").open(&mut open).collapsible(false).resizable(false)
            .default_width(400.0).anchor(Align2::CENTER_CENTER, Vec2::ZERO).show(ctx, |ui| {
            ui.label("Connect your EWI by USB MIDI, choose a voice, and play. Breath shapes both the sound and the ink.");
            ui.label("If your controller uses a different breath message, click the connection status below the canvas to adjust its mapping.");
            ui.separator();
            egui::Grid::new("shortcuts").spacing([24.0, 14.0]).show(ui, |ui| {
                for (key, action) in [("Space", "Start / stop the preview melody"), ("Esc", "Silence everything immediately"),
                    ("F", "Freeze / resume the canvas"), ("C", "Begin a new canvas"), ("?", "Show this guide")] {
                    ui.label(egui::RichText::new(key).strong()); ui.label(action); ui.end_row();
                }
            });
            ui.separator();
            ui.label(egui::RichText::new("Holophonor · first movement\nOriginal synthesis. Local, immediate, yours.").font(italic(17.0)).color(ACCENT));
        });
        self.help_open = open;
    }

    fn capture(&mut self, ctx: &egui::Context) {
        if self.capture.is_none() {
            return;
        }
        if !self.capture_requested
            && self.started.elapsed() > Duration::from_millis(1300)
            && self.frames > 8
        {
            ctx.send_viewport_cmd(egui::ViewportCommand::Screenshot(Default::default()));
            self.capture_requested = true;
        }
        let screenshot = ctx.input(|input| {
            input.events.iter().find_map(|event| {
                if let egui::Event::Screenshot { image, .. } = event {
                    Some(image.clone())
                } else {
                    None
                }
            })
        });
        if let Some(image) = screenshot {
            let path = self.capture.take().unwrap();
            match save_png(&path, &image) {
                Ok(()) => {
                    println!("Captured {path}");
                    ctx.send_viewport_cmd(egui::ViewportCommand::Close);
                }
                Err(error) => {
                    eprintln!("Capture failed: {error}");
                    self.capture_error = Some(error.to_string());
                }
            }
        }
    }
}

impl eframe::App for Holophonor {
    fn ui(&mut self, ui: &mut egui::Ui, _frame: &mut eframe::Frame) {
        let ctx = ui.ctx().clone();
        let now = Instant::now();
        let dt = (now - self.last_frame).as_secs_f32().min(0.05);
        self.last_frame = now;
        self.frames += 1;
        if let Some(audio) = &mut self.audio {
            audio.poll();
        }
        if ctx.input(|i| i.key_pressed(egui::Key::Escape)) {
            self.shared.panic();
            self.instrument_menu = false;
            self.visual_menu = false;
        }
        if !ctx.egui_wants_keyboard_input() {
            if ctx.input(|i| i.key_pressed(egui::Key::Space)) {
                self.toggle_preview();
            }
            if ctx.input(|i| i.key_pressed(egui::Key::F)) {
                self.canvas.frozen = !self.canvas.frozen;
            }
            if ctx.input(|i| i.key_pressed(egui::Key::C)) {
                self.canvas.clear();
            }
            if ctx.input(|i| i.key_pressed(egui::Key::Questionmark)) {
                self.help_open = !self.help_open;
            }
        }
        if self.shared.preview_on.load(Ordering::Relaxed) {
            self.shared.preview.store(
                preview(self.preview_started.elapsed().as_secs_f32()).pack(),
                Ordering::Relaxed,
            );
        }
        let state = self.shared.performance();
        self.canvas.update(state, dt, self.preferences.sensitivity);
        self.draw(ui, &ctx);
        if self.settings_open {
            self.settings(&ctx);
        }
        if self.help_open {
            self.help(&ctx);
        }
        self.capture(&ctx);
        ctx.request_repaint_after(Duration::from_millis(
            if state.gate || !self.canvas.empty() || self.capture.is_some() {
                16
            } else {
                80
            },
        ));
    }

    fn save(&mut self, storage: &mut dyn eframe::Storage) {
        eframe::set_value(storage, "holophonor.preferences", &self.preferences);
    }
    fn clear_color(&self, _: &egui::Visuals) -> [f32; 4] {
        PAPER.to_normalized_gamma_f32()
    }
}

fn preview(time: f32) -> Performance {
    let notes = [60, 62, 64, 67, 69, 67, 64, 62, 60, 55, 57, 60];
    let beat = time / 1.35;
    let fraction = beat.fract();
    let envelope = (fraction * std::f32::consts::PI).sin().max(0.0).powf(0.45);
    Performance {
        note: notes[beat as usize % notes.len()],
        gate: fraction < 0.96,
        breath: 0.66 * envelope,
        expression: 1.0,
        bend: (time * 4.7).sin() * 0.025,
        velocity: 0.7,
    }
}

fn selector(
    ui: &mut egui::Ui,
    rect: Rect,
    id: &str,
    label: &str,
    value: &str,
    visual: bool,
) -> egui::Response {
    let response = ui.interact(rect, ui.id().with(id), egui::Sense::click());
    response.widget_info(|| {
        egui::WidgetInfo::labeled(egui::WidgetType::Button, true, format!("{label}: {value}"))
    });
    let p = ui.painter();
    p.rect_filled(
        rect.translate(Vec2::new(0.0, 2.0)),
        14.0,
        Color32::from_black_alpha(4),
    );
    p.rect_filled(
        rect,
        14.0,
        if response.hovered() {
            Color32::from_rgb(253, 251, 245)
        } else {
            Color32::from_rgb(249, 247, 240)
        },
    );
    p.rect_stroke(rect, 14.0, Stroke::new(0.8, LINE), egui::StrokeKind::Inside);
    p.text(
        rect.left_top() + Vec2::new(18.0, 19.0),
        Align2::LEFT_CENTER,
        label,
        sans(9.0),
        MUTED,
    );
    p.text(
        rect.left_top() + Vec2::new(18.0, 45.0),
        Align2::LEFT_CENTER,
        value,
        serif(if rect.width() < 230.0 { 22.0 } else { 25.0 }),
        INK,
    );
    let center = rect.right_center() + Vec2::new(-22.0, 5.0);
    if visual {
        p.circle_stroke(
            center + Vec2::new(0.0, -14.0),
            5.0,
            Stroke::new(0.7, ACCENT),
        );
    }
    p.line_segment(
        [center + Vec2::new(-3.0, 0.0), center + Vec2::new(0.0, 3.0)],
        Stroke::new(1.0, MUTED),
    );
    p.line_segment(
        [center + Vec2::new(0.0, 3.0), center + Vec2::new(3.0, 0.0)],
        Stroke::new(1.0, MUTED),
    );
    if response.has_focus() {
        p.rect_stroke(
            rect.expand(2.0),
            14.0,
            Stroke::new(1.5, ACCENT),
            egui::StrokeKind::Outside,
        );
    }
    response.on_hover_cursor(egui::CursorIcon::PointingHand)
}

fn small_button(ui: &mut egui::Ui, rect: Rect, id: &str, label: &str) -> egui::Response {
    let response = text_button(ui, rect, id, label);
    ui.painter().rect_stroke(
        rect,
        20.0,
        Stroke::new(
            0.7,
            if response.hovered() || response.has_focus() {
                ACCENT
            } else {
                LINE
            },
        ),
        egui::StrokeKind::Inside,
    );
    response
}

fn text_button(ui: &mut egui::Ui, rect: Rect, id: &str, label: &str) -> egui::Response {
    let response = ui.interact(rect, ui.id().with(id), egui::Sense::click());
    response.widget_info(|| egui::WidgetInfo::labeled(egui::WidgetType::Button, true, label));
    if response.hovered() {
        ui.painter()
            .rect_filled(rect, 20.0, Color32::from_white_alpha(100));
    }
    ui.painter()
        .text(rect.center(), Align2::CENTER_CENTER, label, sans(12.0), INK);
    response.on_hover_cursor(egui::CursorIcon::PointingHand)
}

fn popup_frame() -> egui::Frame {
    egui::Frame::new()
        .fill(Color32::from_rgb(251, 249, 244))
        .stroke(Stroke::new(1.0, LINE))
        .corner_radius(16.0)
        .inner_margin(20.0)
        .shadow(egui::epaint::Shadow {
            offset: [0, 8],
            blur: 30,
            spread: 1,
            color: Color32::from_black_alpha(18),
        })
}

fn draw_emblem(painter: &egui::Painter, center: Pos2, size: f32, color: Color32) {
    for ribbon in 0..3 {
        let points = (0..64)
            .map(|i| {
                let t = i as f32 / 63.0 * std::f32::consts::TAU;
                let angle = t + ribbon as f32 * 0.12;
                center + Vec2::new(angle.sin() * size * 0.7, (angle * 2.0).sin() * size * 0.5)
            })
            .collect();
        painter.add(egui::Shape::line(
            points,
            Stroke::new(
                if ribbon == 1 { 1.0 } else { 0.6 },
                color.gamma_multiply(0.7),
            ),
        ));
    }
}

fn make_paper(ctx: &egui::Context) -> egui::TextureHandle {
    let mut pixels = Vec::with_capacity(384 * 384);
    let mut random = 271828_u32;
    for y in 0..384 {
        for x in 0..384 {
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            let variation = (random % 7) as i16 - 3 + if x % 3 == 0 || y % 4 == 0 { -1 } else { 0 };
            pixels.push(Color32::from_rgb(
                (246 + variation) as u8,
                (243 + variation) as u8,
                (235 + variation) as u8,
            ));
        }
    }
    ctx.load_texture(
        "washi",
        egui::ColorImage::new([384, 384], pixels),
        egui::TextureOptions {
            magnification: egui::TextureFilter::Linear,
            minification: egui::TextureFilter::Linear,
            wrap_mode: egui::TextureWrapMode::Repeat,
            ..Default::default()
        },
    )
}

fn note_name(note: u8) -> String {
    let names = [
        "C", "C♯", "D", "D♯", "E", "F", "F♯", "G", "G♯", "A", "A♯", "B",
    ];
    format!("{}{}", names[note as usize % 12], note as i32 / 12 - 1)
}

fn save_png(path: &str, image: &egui::ColorImage) -> Result<(), Box<dyn std::error::Error>> {
    let file = std::fs::File::create(path)?;
    let mut encoder = png::Encoder::new(
        std::io::BufWriter::new(file),
        image.width() as u32,
        image.height() as u32,
    );
    encoder.set_color(png::ColorType::Rgba);
    encoder.set_depth(png::BitDepth::Eight);
    let pixels: Vec<u8> = image
        .pixels
        .iter()
        .flat_map(|pixel| pixel.to_array())
        .collect();
    encoder.write_header()?.write_image_data(&pixels)?;
    Ok(())
}
