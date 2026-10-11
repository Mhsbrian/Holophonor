use eframe::egui::{self, Color32, Pos2, Rect, Shape, Stroke, Vec2};
use holophonor::midi::Performance;
use std::collections::VecDeque;

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub enum ArtMode {
    #[default]
    Ink,
    Bloom,
    Stars,
}

impl ArtMode {
    pub const ALL: [Self; 3] = [Self::Ink, Self::Bloom, Self::Stars];
    pub fn name(self) -> &'static str {
        match self {
            Self::Ink => "Ink in water",
            Self::Bloom => "Watercolor",
            Self::Stars => "Constellation",
        }
    }
    pub fn description(self) -> &'static str {
        match self {
            Self::Ink => "Breath becomes a flowing silk of ink.",
            Self::Bloom => "Each phrase leaves a wash of color.",
            Self::Stars => "Your notes find their place in the sky.",
        }
    }
}

#[derive(Clone, Copy)]
struct Mark {
    position: Pos2,
    pressure: f32,
    hue: usize,
    age: f32,
}

pub struct Canvas {
    marks: VecDeque<Mark>,
    phase: f32,
    previous: Pos2,
    pub frozen: bool,
}

impl Default for Canvas {
    fn default() -> Self {
        Self {
            marks: VecDeque::with_capacity(900),
            phase: 0.0,
            previous: Pos2::new(0.5, 0.5),
            frozen: false,
        }
    }
}

impl Canvas {
    pub fn clear(&mut self) {
        self.marks.clear();
        self.phase = 0.0;
        self.previous = Pos2::new(0.5, 0.5);
    }
    pub fn empty(&self) -> bool {
        self.marks.is_empty()
    }

    pub fn update(&mut self, state: Performance, dt: f32, sensitivity: f32) {
        if self.frozen {
            return;
        }
        for mark in &mut self.marks {
            mark.age += dt;
        }
        while self.marks.front().is_some_and(|m| m.age > 32.0) {
            self.marks.pop_front();
        }
        if !state.gate || state.breath < 0.01 {
            return;
        }
        self.phase += dt * (0.28 + state.breath * 0.6) * sensitivity;
        let pitch = (state.note as f32 - 60.0) / 24.0 + state.bend * 0.1;
        let t = self.phase;
        let desired = Pos2::new(
            0.5 + (t * 0.83).sin() * 0.30 + (t * 2.1 + pitch).cos() * 0.075,
            0.5 + (t * 1.37 + pitch * 0.8).cos() * 0.26 + (t * 0.7).sin() * 0.055,
        );
        if self.marks.is_empty() {
            self.previous = desired;
        }
        self.previous += (desired - self.previous) * (dt * 9.0).min(1.0);
        if self.marks.len() == 900 {
            self.marks.pop_front();
        }
        self.marks.push_back(Mark {
            position: self.previous,
            pressure: state.breath,
            hue: (state.note as usize / 3) % 4,
            age: 0.0,
        });
    }

    pub fn paint(&self, painter: &egui::Painter, rect: Rect, mode: ArtMode) {
        let painter = painter.with_clip_rect(rect);
        let point = |mark: &Mark| {
            Pos2::new(
                rect.left() + rect.width() * mark.position.x,
                rect.top() + rect.height() * mark.position.y,
            )
        };
        let palette = [
            Color32::from_rgb(91, 121, 110),
            Color32::from_rgb(132, 123, 152),
            Color32::from_rgb(181, 125, 96),
            Color32::from_rgb(89, 123, 139),
        ];
        match mode {
            ArtMode::Ink => {
                // Translucent meshes layer like pigment, with a fine core line.
                for layer in (0..5).rev() {
                    let mut mesh = egui::Mesh::default();
                    let mut previous = None;
                    for (i, mark) in self.marks.iter().enumerate() {
                        let p = point(mark);
                        let tangent: Vec2 = previous.map(|prev: Pos2| p - prev).unwrap_or(Vec2::X);
                        let normal = Vec2::new(-tangent.y, tangent.x).normalized();
                        let fade = ((32.0 - mark.age) / 32.0).clamp(0.0, 1.0);
                        let width = (2.0 + mark.pressure * 13.0) * (1.0 + layer as f32 * 0.85);
                        let color = palette[mark.hue]
                            .gamma_multiply(fade * if layer == 0 { 0.21 } else { 0.045 });
                        mesh.colored_vertex(p + normal * width, color);
                        mesh.colored_vertex(p - normal * width, color);
                        if i > 0 {
                            let index = (i * 2) as u32;
                            mesh.add_triangle(index - 2, index - 1, index);
                            mesh.add_triangle(index - 1, index + 1, index);
                        }
                        previous = Some(p);
                    }
                    painter.add(Shape::mesh(mesh));
                }
                let mut previous = None;
                for mark in &self.marks {
                    let p = point(mark);
                    if let Some(last) = previous {
                        let fade = (1.0 - mark.age / 32.0).clamp(0.0, 1.0);
                        painter.line_segment(
                            [last, p],
                            Stroke::new(0.7, palette[mark.hue].gamma_multiply(0.3 * fade)),
                        );
                    }
                    previous = Some(p);
                }
            }
            ArtMode::Bloom => {
                for (i, mark) in self.marks.iter().enumerate().step_by(8) {
                    let p = point(mark);
                    let fade = (1.0 - mark.age / 32.0).clamp(0.0, 1.0);
                    let size = (12.0 + mark.pressure * 35.0) * rect.height() / 500.0;
                    for layer in (1..=5).rev() {
                        let offset = Vec2::angled(i as f32 * 2.4) * layer as f32 * 1.8;
                        painter.circle_filled(
                            p + offset,
                            size * layer as f32 / 3.0,
                            palette[mark.hue].gamma_multiply(0.018 * fade),
                        );
                    }
                    painter.circle_stroke(
                        p,
                        size * 0.9,
                        Stroke::new(0.4, palette[mark.hue].gamma_multiply(0.13 * fade)),
                    );
                }
            }
            ArtMode::Stars => {
                let mut previous = None;
                for mark in self.marks.iter().step_by(20) {
                    let p = point(mark);
                    let fade = (1.0 - mark.age / 32.0).clamp(0.0, 1.0);
                    let color = palette[mark.hue].gamma_multiply(fade * 0.6);
                    if let Some(last) = previous {
                        painter
                            .line_segment([last, p], Stroke::new(0.65, color.gamma_multiply(0.4)));
                    }
                    painter.circle_filled(p, 1.5 + mark.pressure * 1.7, color);
                    painter.circle_stroke(
                        p,
                        7.0 + mark.pressure * 6.0,
                        Stroke::new(0.5, color.gamma_multiply(0.2)),
                    );
                    previous = Some(p);
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn canvas_is_bounded_and_freeze_does_not_age_marks() {
        let mut canvas = Canvas::default();
        let state = Performance {
            gate: true,
            breath: 0.8,
            ..Performance::default()
        };
        for _ in 0..2000 {
            canvas.update(state, 1.0 / 60.0, 1.0);
        }
        assert!(canvas.marks.len() <= 900);
        canvas.frozen = true;
        let age = canvas.marks[0].age;
        canvas.update(state, 10.0, 1.0);
        assert_eq!(canvas.marks[0].age, age);
        canvas.frozen = false;
        canvas.update(Performance::default(), 33.0, 1.0);
        assert!(canvas.empty());
    }
}
