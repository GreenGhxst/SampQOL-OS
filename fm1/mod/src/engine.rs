// SPDX-License-Identifier: GPL-3.0-only
//! Safe wrapper around the open FM1 DX7 engine (C, from m-vave/fm1_open) and
//! the voice bank. One `Engine` belongs to the audio thread.

use core::ffi::{c_char, c_int};

/// Expanded DX7 voice size.
pub const PATCH_BYTES: usize = 156;
/// The engine renders in blocks of this many mono samples.
pub const BLOCK: usize = 64;

#[repr(C)]
struct SynthDx7 {
    _private: [u8; 0],
}

unsafe extern "C" {
    fn synth_dx7_create(max_voices: u8, sample_rate: u32, block_samples: u16) -> *mut SynthDx7;
    fn synth_dx7_destroy(synth: *mut SynthDx7);
    fn synth_dx7_midi(
        synth: *mut SynthDx7,
        patch: *mut u8,
        message: *const u8,
        bytes: usize,
    ) -> c_int;
    fn synth_dx7_lfo_reset(synth: *mut SynthDx7, patch: *const u8) -> c_int;
    fn synth_dx7_render(synth: *mut SynthDx7, output: *mut i32, samples: usize) -> c_int;
    fn synth_dx7_set_poly_mode(synth: *mut SynthDx7, mono: u8);
    fn synth_dx7_set_operator_mask(synth: *mut SynthDx7, mask: u8);
    fn synth_dx7_set_adsr(synth: *mut SynthDx7, enabled: u8, a: u8, d: u8, s: u8, r: u8);
    fn synth_dx7_set_pitch_bend_range(synth: *mut SynthDx7, up: u8, down: u8);
    fn synth_dx7_active(synth: *const SynthDx7) -> u32;
    fn fm1_dx7_panic(synth: *mut SynthDx7);
    fn fm1_bank_patch(patch: *mut u8, program: u32);
    fn fm1_bank_name(name: *mut c_char, program: u32);
}

/// Name of a factory voice, 0-based program, trimmed.
#[must_use]
pub fn voice_name(program: u8) -> String {
    let mut raw = [0_u8; 11];
    // SAFETY: the C side writes 10 bytes and a NUL into an 11-byte buffer and
    // only reads static tables.
    unsafe { fm1_bank_name(raw.as_mut_ptr().cast::<c_char>(), u32::from(program)) };
    let end = raw.iter().position(|b| *b == 0).unwrap_or(10);
    String::from_utf8_lossy(&raw[..end]).trim().to_string()
}

/// The synth plus its current voice and a partially consumed render block.
pub struct Engine {
    synth: *mut SynthDx7,
    rate: u32,
    voices: u8,
    patch: [u8; PATCH_BYTES],
    pend: [i32; BLOCK],
    pos: usize,
    program: u8,
}

// SAFETY: the raw pointer is only ever used by the thread that owns the
// `Engine` (the audio thread); it is handed over before audio starts.
unsafe impl Send for Engine {}

impl Engine {
    /// Create the synth; `None` when the engine rejects the rate.
    #[must_use]
    pub fn new(rate: u32, voices: u8) -> Option<Self> {
        // SAFETY: plain constructor call; the result is checked for NULL.
        let synth = unsafe { synth_dx7_create(voices.clamp(1, 16), rate, BLOCK as u16) };
        if synth.is_null() {
            return None;
        }
        let mut engine = Self {
            synth,
            rate,
            voices,
            patch: [0; PATCH_BYTES],
            pend: [0; BLOCK],
            pos: BLOCK,
            program: 0,
        };
        // SAFETY: `synth` is live.
        unsafe { synth_dx7_set_pitch_bend_range(synth, 2, 2) };
        engine.set_program(0);
        Some(engine)
    }

    /// Sample rate the synth was created for.
    #[must_use]
    pub const fn rate(&self) -> u32 {
        self.rate
    }

    /// Current 0-based program.
    #[must_use]
    pub const fn program(&self) -> u8 {
        self.program
    }

    /// Load factory voice `program` (0..=127) as in the FM1 app.
    pub fn set_program(&mut self, program: u8) {
        self.program = program & 127;
        // SAFETY: the patch buffer has PATCH_BYTES bytes; the synth is live.
        unsafe {
            fm1_bank_patch(self.patch.as_mut_ptr(), u32::from(self.program));
            synth_dx7_set_operator_mask(self.synth, self.patch[155]);
            synth_dx7_set_poly_mode(self.synth, 0);
            synth_dx7_set_adsr(self.synth, 0, 0, 0, 100, 0);
            synth_dx7_lfo_reset(self.synth, self.patch.as_ptr());
        }
    }

    /// One complete channel message (2 or 3 bytes). Program changes are
    /// handled here; everything else goes to the engine.
    pub fn midi(&mut self, message: &[u8]) {
        let Some(&status) = message.first() else {
            return;
        };
        let cmd = status & 0xF0;
        if cmd == 0xC0 {
            if let Some(&program) = message.get(1) {
                self.set_program(program);
            }
            return;
        }
        let len = if cmd == 0xC0 || cmd == 0xD0 { 2 } else { 3 };
        if message.len() < len {
            return;
        }
        let normalized = [cmd, message[1], if len == 3 { message[2] } else { 0 }];
        // SAFETY: both buffers are valid for the stated lengths; the synth is live.
        unsafe {
            synth_dx7_midi(
                self.synth,
                self.patch.as_mut_ptr(),
                normalized.as_ptr(),
                len,
            );
        }
    }

    /// Release every voice at once.
    pub fn panic(&mut self) {
        // SAFETY: the synth is live.
        unsafe { fm1_dx7_panic(self.synth) };
    }

    /// Voices sounding right now.
    #[must_use]
    pub fn active(&self) -> u32 {
        // SAFETY: the synth is live.
        unsafe { synth_dx7_active(self.synth) }
    }

    /// Voice count the engine was created with.
    #[must_use]
    pub const fn voices(&self) -> u8 {
        self.voices
    }

    /// Render `out.len()` mono samples scaled by `gain` (1.0 = Q24 full scale).
    pub fn render(&mut self, out: &mut [f32], gain: f32) {
        let scale = gain / 16_777_216.0;
        let mut i = 0;
        while i < out.len() {
            if self.pos >= BLOCK {
                self.pend = [0; BLOCK];
                // SAFETY: `pend` holds BLOCK samples; the synth is live.
                unsafe { synth_dx7_render(self.synth, self.pend.as_mut_ptr(), BLOCK) };
                self.pos = 0;
            }
            let n = (BLOCK - self.pos).min(out.len() - i);
            for k in 0..n {
                out[i + k] = self.pend[self.pos + k] as f32 * scale;
            }
            self.pos += n;
            i += n;
        }
    }
}

impl Drop for Engine {
    fn drop(&mut self) {
        // SAFETY: created by `synth_dx7_create`, destroyed once.
        unsafe { synth_dx7_destroy(self.synth) };
    }
}

#[cfg(test)]
mod tests {
    use super::{Engine, voice_name};

    #[test]
    fn the_factory_bank_has_names() {
        assert_eq!(voice_name(0), "PIANO 1");
        assert!(!voice_name(127).is_empty());
    }

    #[test]
    fn a_note_makes_sound_and_a_program_change_switches_the_voice() {
        let mut engine = Engine::new(44_100, 8).expect("44.1 kHz is supported");
        let mut out = vec![0.0_f32; 4096];
        engine.render(&mut out, 1.0);
        assert!(out.iter().all(|s| *s == 0.0), "silent before a note");
        engine.midi(&[0x90, 60, 100]);
        engine.render(&mut out, 1.0);
        let rms = (out.iter().map(|s| s * s).sum::<f32>() / out.len() as f32).sqrt();
        assert!(rms > 0.001, "rms {rms}");
        assert!(
            out.iter().all(|s| s.abs() <= 1.0),
            "no clipping at unity gain"
        );
        engine.midi(&[0xC0, 9]);
        assert_eq!(engine.program(), 9);
        engine.midi(&[0x80, 60, 0]);
        engine.panic();
        assert_eq!(engine.active(), 0);
    }

    #[test]
    fn unsupported_rates_are_refused() {
        assert!(Engine::new(1_000_000, 8).is_none());
    }
}
