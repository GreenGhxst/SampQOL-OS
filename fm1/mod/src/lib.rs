// SPDX-License-Identifier: GPL-3.0-only
//! fm1mod: the open FM1 DX7 synth as an MPC Sample mod.
//!
//! SHIFT + DATA (encoder push) opens the FM1 panel on the Sample page. While
//! it is open the 16 pads play the synth chromatically, the DATA encoder
//! selects one of the 128 FM1 factory voices (SHIFT + turn: volume), PLUS and
//! MINUS move the pad octave, and an external MIDI keyboard plays the synth.
//! The sound is added to the MPC's output in the app's own audio callback.

pub mod engine;
pub mod ring;

use core::sync::atomic::{AtomicU8, Ordering};

use mpc_sample_sdk::audio::{self, OutputInfo};
use mpc_sample_sdk::draw::{self, Graphics};
use mpc_sample_sdk::platform::status;
use mpc_sample_sdk::{
    ButtonEvent, ButtonFlow, EncoderEvent, EncoderFlow, Mod, NoteEvent, NoteFlow, PadAction,
    PadEvent, PadFlow, PaintEvent,
};

use engine::Engine;
use ring::Ring;

const KEY_SHIFT: u32 = 0x31;
const KEY_DATA: u32 = 0x6f;
const KEY_PLUS: u32 = 0x36;
const KEY_MINUS: u32 = 0x37;
const MAX_FRAMES: usize = 4096;
const VOICES: u8 = 8;
/// Linear gain at volume 127: leaves headroom for the MPC's own mix.
const GAIN: f32 = 0.5;

/// UI -> audio messages.
static RING: Ring = Ring::new();
/// Volume 0..127, written by the UI, read by the audio thread.
static VOLUME: AtomicU8 = AtomicU8::new(100);
/// The program the audio thread currently plays (for the panel).
static PLAYING: AtomicU8 = AtomicU8::new(0);
/// Sounding voices, for the panel.
static ACTIVE: AtomicU8 = AtomicU8::new(0);

/// State the UI thread owns.
#[derive(Default)]
struct Ui {
    open: bool,
    program: u8,
    octave: i8,
    held: [u8; 16],
}

impl Ui {
    fn base_note(&self) -> i32 {
        48 + i32::from(self.octave) * 12
    }
}

/// State the audio thread owns.
#[derive(Default)]
struct AudioSide {
    engine: Option<Engine>,
    mono: Vec<f32>,
    rate: u32,
}

/// The mod.
#[derive(Default)]
pub struct Fm1Mod {
    ui: Ui,
    audio: AudioSide,
}

fn velocity_byte(velocity: f32) -> u8 {
    let v = if velocity <= 1.0 {
        velocity * 127.0
    } else {
        velocity
    };
    (v.round() as i32).clamp(1, 127) as u8
}

impl Fm1Mod {
    fn note_on(&self, note: i32, velocity: u8) {
        if (0..=127).contains(&note) {
            RING.push(0x90, note as u8, velocity);
        }
    }

    fn note_off(&self, note: i32) {
        if (0..=127).contains(&note) {
            RING.push(0x80, note as u8, 0);
        }
    }

    fn all_off(&mut self) {
        for pad in 0..16 {
            if self.ui.held[pad] != 0 {
                let note = i32::from(self.ui.held[pad]);
                self.note_off(note);
                self.ui.held[pad] = 0;
            }
        }
        RING.push(0xB0, 123, 0);
    }

    fn show(&self) {
        let name = engine::voice_name(self.ui.program);
        status::set_status(&format!(
            "FM1 {:03} {}  vol {}  oct {}",
            self.ui.program + 1,
            name,
            VOLUME.load(Ordering::Relaxed),
            self.ui.octave
        ));
        draw::repaint_painted();
    }
}

impl Mod for Fm1Mod {
    fn on_start(&mut self) {
        self.audio.mono = vec![0.0; MAX_FRAMES];
        // Create the synth here, outside the audio thread; it is rebuilt once
        // if the stream turns out to run at another rate.
        self.audio.engine = Engine::new(44_100, VOICES);
        self.audio.rate = 44_100;
        let _ = engine::voice_name(0); // expand the factory bank now
        status::set_status("FM1 synth: SHIFT + DATA opens it");
    }

    fn on_button(&mut self, event: ButtonEvent) -> ButtonFlow {
        if event.code == KEY_SHIFT {
            return ButtonFlow::Forward;
        }
        if event.code == KEY_DATA && event.pressed && event.shift {
            self.ui.open = !self.ui.open;
            if self.ui.open {
                RING.push(0xC0, self.ui.program, 0);
                self.show();
            } else {
                self.all_off();
                status::set_status("FM1 closed");
                draw::repaint_painted();
            }
            return ButtonFlow::Consume;
        }
        if event.code == KEY_DATA && event.shift {
            return ButtonFlow::Consume; // the release of the chord
        }
        if self.ui.open && (event.code == KEY_PLUS || event.code == KEY_MINUS) {
            if event.pressed {
                self.all_off();
                let step = if event.code == KEY_PLUS { 1 } else { -1 };
                self.ui.octave = (self.ui.octave + step).clamp(-3, 3);
                self.show();
            }
            return ButtonFlow::Consume;
        }
        ButtonFlow::Forward
    }

    fn on_pad(&mut self, event: PadEvent) -> PadFlow {
        if !self.ui.open || !(0..16).contains(&event.pad) {
            return PadFlow::Forward;
        }
        let pad = event.pad as usize;
        match event.action {
            PadAction::Press => {
                let note = self.ui.base_note() + event.pad;
                if self.ui.held[pad] != 0 {
                    self.note_off(i32::from(self.ui.held[pad]));
                }
                self.note_on(note, velocity_byte(event.velocity));
                self.ui.held[pad] = note.clamp(0, 127) as u8;
            }
            PadAction::Release => {
                if self.ui.held[pad] != 0 {
                    self.note_off(i32::from(self.ui.held[pad]));
                    self.ui.held[pad] = 0;
                }
            }
        }
        PadFlow::Consume
    }

    fn on_encoder(&mut self, event: EncoderEvent) -> EncoderFlow {
        if !self.ui.open || event.delta == 0 {
            return EncoderFlow::Forward;
        }
        let step = event.delta.signum();
        if mpc_sample_sdk::shift() {
            let volume = (i32::from(VOLUME.load(Ordering::Relaxed)) + step * 4).clamp(0, 127);
            VOLUME.store(volume as u8, Ordering::Relaxed);
        } else {
            self.ui.program = (i32::from(self.ui.program) + step).rem_euclid(128) as u8;
            RING.push(0xC0, self.ui.program, 0);
        }
        self.show();
        EncoderFlow::Consume
    }

    fn on_note(&mut self, event: &mut NoteEvent) -> NoteFlow {
        if !self.ui.open || !event.external {
            return NoteFlow::Forward;
        }
        if event.note_on {
            RING.push(0x90, event.note & 127, velocity_byte(event.velocity));
        } else if event.note_off {
            RING.push(0x80, event.note & 127, 0);
        } else {
            return NoteFlow::Forward;
        }
        NoteFlow::Consume
    }

    fn on_paint(&mut self, graphics: &mut Graphics, event: PaintEvent) {
        if !event.sample_page || !self.ui.open {
            return;
        }
        let program = PLAYING.load(Ordering::Relaxed);
        let name = engine::voice_name(program);
        graphics.colour(0xf010_1014);
        graphics.rect(6, 6, 308, 62);
        graphics.colour(0xffff_cc00);
        graphics.text("FM1 SYNTH", 12, 9, 120, 16, 13.0);
        graphics.colour(0xffff_ffff);
        graphics.text(
            &format!("{:03}  {}", program + 1, name),
            12,
            27,
            220,
            18,
            15.0,
        );
        graphics.colour(0xffc0_c0c0);
        graphics.text(
            &format!(
                "vol {:3}  oct {:+}  voices {}/{}",
                VOLUME.load(Ordering::Relaxed),
                self.ui.octave,
                ACTIVE.load(Ordering::Relaxed),
                VOICES
            ),
            12,
            47,
            200,
            14,
            11.0,
        );
        graphics.text("DATA voice  SHIFT+DATA vol  +/- oct", 140, 9, 170, 14, 9.0);
    }

    fn on_output(&mut self, frames: &mut [i32], info: &OutputInfo) {
        if info.channels == 0 || frames.is_empty() {
            return;
        }
        let count = (frames.len() / info.channels).min(MAX_FRAMES);
        if self.audio.mono.len() < MAX_FRAMES {
            return; // on_start has not run
        }
        if self
            .audio
            .engine
            .as_ref()
            .is_none_or(|e| e.rate() != info.rate)
        {
            // Happens once, when the device rate differs from the default.
            let program = self.audio.engine.as_ref().map_or(0, Engine::program);
            self.audio.engine = Engine::new(info.rate, VOICES);
            if let Some(engine) = self.audio.engine.as_mut() {
                engine.set_program(program);
            }
            self.audio.rate = info.rate;
        }
        let Some(engine) = self.audio.engine.as_mut() else {
            return;
        };
        while let Some(message) = RING.pop() {
            engine.midi(&message);
        }
        PLAYING.store(engine.program(), Ordering::Relaxed);
        let gain = GAIN * f32::from(VOLUME.load(Ordering::Relaxed)) / 127.0;
        engine.render(&mut self.audio.mono[..count], gain);
        ACTIVE.store(engine.active().min(255) as u8, Ordering::Relaxed);
        let mono = &self.audio.mono[..count];
        let planes: [&[f32]; 2] = [mono, mono];
        let _ = audio::add_planes_i32(
            &mut frames[..count * info.channels],
            info.channels,
            &planes[..info.channels.min(2)],
        );
    }
}

mpc_sample_sdk::export_mod!(Fm1Mod);

#[cfg(test)]
mod tests {
    use super::velocity_byte;

    #[test]
    fn velocities_in_both_scales_map_to_midi() {
        assert_eq!(velocity_byte(1.0), 127);
        assert_eq!(velocity_byte(0.5), 64);
        assert_eq!(velocity_byte(0.0), 1);
        assert_eq!(velocity_byte(100.0), 100);
    }
}
