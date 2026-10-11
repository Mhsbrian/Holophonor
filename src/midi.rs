use serde::{Deserialize, Serialize};

/// A whole performance state fits in one atomic word, so the audio thread sees
/// coherent controls and never needs to drain an unbounded stream of CC events.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Performance {
    pub note: u8,
    pub gate: bool,
    pub breath: f32,
    pub expression: f32,
    pub bend: f32,
    pub velocity: f32,
}

impl Default for Performance {
    fn default() -> Self {
        Self {
            note: 60,
            gate: false,
            breath: 0.0,
            expression: 1.0,
            bend: 0.0,
            velocity: 0.0,
        }
    }
}

impl Performance {
    pub fn pack(self) -> u64 {
        let breath = (self.breath.clamp(0.0, 1.0) * 16383.0).round() as u64;
        let expression = (self.expression.clamp(0.0, 1.0) * 127.0).round() as u64;
        let bend = ((self.bend.clamp(-1.0, 1.0) + 1.0) * 8191.5).round() as u64;
        let velocity = (self.velocity.clamp(0.0, 1.0) * 127.0).round() as u64;
        u64::from(self.note.min(127))
            | ((self.gate as u64) << 7)
            | (breath << 8)
            | (expression << 22)
            | (bend << 29)
            | (velocity << 43)
    }

    pub fn unpack(word: u64) -> Self {
        let raw_bend = ((word >> 29) & 16383) as i32;
        Self {
            note: (word & 127) as u8,
            gate: word & 128 != 0,
            breath: ((word >> 8) & 16383) as f32 / 16383.0,
            expression: ((word >> 22) & 127) as f32 / 127.0,
            bend: (raw_bend - 8192) as f32 / if raw_bend < 8192 { 8192.0 } else { 8191.0 },
            velocity: ((word >> 43) & 127) as f32 / 127.0,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(default)]
pub struct Profile {
    pub breath_cc: u8,
    /// Zero listens to all channels; 1–16 selects a single channel.
    pub channel: u8,
    pub floor: u8,
    pub ceiling: u8,
    pub velocity_breath: bool,
}

impl Default for Profile {
    fn default() -> Self {
        Self {
            breath_cc: 2,
            channel: 0,
            floor: 0,
            ceiling: 127,
            velocity_breath: false,
        }
    }
}

impl Profile {
    pub fn pack(self) -> u64 {
        u64::from(self.breath_cc.min(127))
            | (u64::from(self.channel.min(16)) << 7)
            | (u64::from(self.floor.min(126)) << 12)
            | (u64::from(self.ceiling.max(self.floor.saturating_add(1)).min(127)) << 19)
            | ((self.velocity_breath as u64) << 26)
    }

    pub fn unpack(word: u64) -> Self {
        Self {
            breath_cc: (word & 127) as u8,
            channel: ((word >> 7) & 31) as u8,
            floor: ((word >> 12) & 127) as u8,
            ceiling: ((word >> 19) & 127) as u8,
            velocity_breath: word & (1 << 26) != 0,
        }
    }
}

pub struct Interpreter {
    pub state: Performance,
    pub profile: Profile,
    held: [u8; 128],
    order: [u8; 128],
    count: usize,
    sustain: bool,
    breath_msb: u8,
    breath_lsb: u8,
    high_resolution: bool,
}

impl Interpreter {
    pub fn new(profile: Profile) -> Self {
        Self {
            state: Performance::default(),
            profile,
            held: [0; 128],
            order: [0; 128],
            count: 0,
            sustain: false,
            breath_msb: 0,
            breath_lsb: 0,
            high_resolution: false,
        }
    }

    pub fn reset(&mut self) {
        *self = Self::new(self.profile);
    }

    fn remove(&mut self, note: u8) {
        if let Some(i) = self.order[..self.count].iter().position(|n| *n == note) {
            self.order.copy_within(i + 1..self.count, i);
            self.count -= 1;
        }
        self.held[note as usize] = 0;
    }

    fn release(&mut self, note: u8) {
        self.remove(note);
        if let Some(&previous) = self.order[..self.count].last() {
            self.state.note = previous;
            self.state.velocity = self.held[previous as usize] as f32 / 127.0;
        } else if !self.sustain {
            self.state.gate = false;
        }
    }

    fn breath(&mut self) {
        let raw = if self.high_resolution {
            ((u16::from(self.breath_msb) << 7) | u16::from(self.breath_lsb)) as f32 / 129.0
        } else {
            self.breath_msb as f32
        };
        let floor = self.profile.floor as f32;
        let ceiling = self
            .profile
            .ceiling
            .max(self.profile.floor.saturating_add(1)) as f32;
        self.state.breath = ((raw - floor) / (ceiling - floor)).clamp(0.0, 1.0);
    }

    /// Returns true only for a new articulation, allowing panic to remain
    /// latched through breath updates until the player starts a fresh note.
    pub fn message(&mut self, bytes: &[u8]) -> bool {
        let Some(&status) = bytes.first() else {
            return false;
        };
        if status == 0xff {
            self.reset();
            return false;
        }
        if !(0x80..0xf0).contains(&status)
            || (self.profile.channel != 0 && (status & 15) + 1 != self.profile.channel)
            || bytes.len() < 3
            || bytes[1] > 127
            || bytes[2] > 127
        {
            return false;
        }
        let (a, b) = (bytes[1], bytes[2]);
        match status & 0xf0 {
            0x90 if b > 0 => {
                self.remove(a);
                self.order[self.count] = a;
                self.count += 1;
                self.held[a as usize] = b;
                self.state.note = a;
                self.state.velocity = b as f32 / 127.0;
                self.state.gate = true;
                if self.profile.velocity_breath {
                    self.state.breath = self.state.velocity;
                }
                return true;
            }
            0x80 | 0x90 => self.release(a),
            0xe0 => {
                let bend = ((u16::from(b) << 7) | u16::from(a)) as i32 - 8192;
                self.state.bend = bend as f32 / if bend < 0 { 8192.0 } else { 8191.0 };
            }
            0xb0 => match a {
                120 | 123 => self.reset(),
                121 => {
                    self.sustain = false;
                    self.state.bend = 0.0;
                    self.state.expression = 1.0;
                    self.breath_msb = 0;
                    self.breath_lsb = 0;
                    self.high_resolution = false;
                    self.state.breath = 0.0;
                    if self.count == 0 {
                        self.state.gate = false;
                    }
                }
                64 => {
                    self.sustain = b >= 64;
                    if !self.sustain && self.count == 0 {
                        self.state.gate = false;
                    }
                }
                cc if cc == self.profile.breath_cc => {
                    self.breath_msb = b;
                    self.breath();
                }
                cc if self.profile.breath_cc < 32 && cc == self.profile.breath_cc + 32 => {
                    self.breath_lsb = b;
                    self.high_resolution = true;
                    self.breath();
                }
                11 => self.state.expression = b as f32 / 127.0,
                _ => {}
            },
            _ => {}
        }
        false
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn breath_is_required_and_high_resolution_is_preserved() {
        let mut midi = Interpreter::new(Profile::default());
        assert!(midi.message(&[0x90, 60, 100]));
        assert!(midi.state.gate);
        assert_eq!(midi.state.breath, 0.0);
        midi.message(&[0xb0, 2, 64]);
        let coarse = midi.state.breath;
        midi.message(&[0xb0, 34, 127]);
        let fine = midi.state.breath;
        midi.message(&[0xb0, 34, 0]);
        assert!(midi.state.breath < fine);
        assert!((fine - coarse).abs() < 0.008);
        midi.message(&[0xb0, 34, 127]);
        midi.message(&[0xb0, 2, 127]);
        assert_eq!(midi.state.breath, 1.0);
        midi.message(&[0xb0, 2, 0]);
        midi.message(&[0xb0, 34, 0]);
        assert_eq!(midi.state.breath, 0.0);
    }

    #[test]
    fn seven_bit_breath_reaches_both_endpoints() {
        let mut midi = Interpreter::new(Profile::default());
        midi.message(&[0xb0, 2, 127]);
        assert_eq!(midi.state.breath, 1.0);
        midi.message(&[0xb0, 2, 0]);
        assert_eq!(midi.state.breath, 0.0);
    }

    #[test]
    fn overlapping_notes_use_last_note_priority_and_zero_velocity_releases() {
        let mut midi = Interpreter::new(Profile::default());
        midi.message(&[0x90, 60, 90]);
        midi.message(&[0x90, 64, 100]);
        midi.message(&[0x90, 64, 0]);
        assert_eq!(midi.state.note, 60);
        assert!(midi.state.gate);
        midi.message(&[0x80, 60, 0]);
        assert!(!midi.state.gate);
    }

    #[test]
    fn sustain_and_panic_do_not_leave_stuck_notes() {
        let mut midi = Interpreter::new(Profile::default());
        midi.message(&[0x90, 69, 127]);
        midi.message(&[0xb0, 64, 127]);
        midi.message(&[0x80, 69, 0]);
        assert!(midi.state.gate);
        midi.message(&[0xb0, 64, 0]);
        assert!(!midi.state.gate);
        midi.message(&[0x90, 69, 127]);
        midi.message(&[0xb0, 123, 0]);
        assert!(!midi.state.gate);
        assert_eq!(midi.state.breath, 0.0);
    }

    #[test]
    fn calibration_channel_filter_and_expression_profile() {
        let mut midi = Interpreter::new(Profile {
            channel: 2,
            breath_cc: 11,
            floor: 10,
            ceiling: 100,
            ..Profile::default()
        });
        midi.message(&[0x90, 60, 127]);
        assert!(!midi.state.gate);
        midi.message(&[0x91, 60, 127]);
        midi.message(&[0xb1, 11, 5]);
        assert_eq!(midi.state.breath, 0.0);
        midi.message(&[0xb1, 11, 127]);
        assert_eq!(midi.state.breath, 1.0);
        assert_eq!(midi.state.expression, 1.0);
    }

    #[test]
    fn bend_endpoints_and_atomic_snapshot_round_trip() {
        let mut midi = Interpreter::new(Profile::default());
        for (bytes, expected) in [
            ([0xe0, 0, 0], -1.0),
            ([0xe0, 0, 64], 0.0),
            ([0xe0, 127, 127], 1.0),
        ] {
            midi.message(&bytes);
            assert_eq!(midi.state.bend, expected);
            let restored = Performance::unpack(midi.state.pack());
            assert!((restored.bend - expected).abs() < 0.0002);
        }
        assert_eq!(
            Profile::unpack(Profile::default().pack()),
            Profile::default()
        );
    }

    #[test]
    fn malformed_messages_and_repeated_notes_are_safe() {
        let mut midi = Interpreter::new(Profile::default());
        for msg in [
            &[][..],
            &[0x90][..],
            &[0x90, 60][..],
            &[0x90, 255, 255][..],
            &[0xfe][..],
        ] {
            midi.message(msg);
        }
        for _ in 0..256 {
            midi.message(&[0x90, 60, 100]);
        }
        assert_eq!(midi.count, 1);
        midi.message(&[0x80, 60, 0]);
        assert!(!midi.state.gate);
    }
}
