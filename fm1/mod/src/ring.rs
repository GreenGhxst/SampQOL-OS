// SPDX-License-Identifier: GPL-3.0-only
//! A small lock-free queue of MIDI messages from the UI and MIDI threads to
//! the audio thread. Producers serialise on a mutex (they may block); the one
//! consumer never locks.

use core::sync::atomic::{AtomicU32, AtomicUsize, Ordering};
use std::sync::Mutex;

const SIZE: usize = 256;

/// The queue. Messages are packed as `status << 16 | data1 << 8 | data2`.
pub struct Ring {
    slots: [AtomicU32; SIZE],
    head: AtomicUsize,
    tail: AtomicUsize,
    producer: Mutex<()>,
}

impl Ring {
    /// An empty queue.
    #[must_use]
    pub const fn new() -> Self {
        Self {
            slots: [const { AtomicU32::new(0) }; SIZE],
            head: AtomicUsize::new(0),
            tail: AtomicUsize::new(0),
            producer: Mutex::new(()),
        }
    }

    /// Queue one message. A full queue drops it.
    pub fn push(&self, status: u8, data1: u8, data2: u8) {
        let guard = self.producer.lock();
        let head = self.head.load(Ordering::Relaxed);
        let tail = self.tail.load(Ordering::Acquire);
        if head.wrapping_sub(tail) < SIZE {
            let packed = u32::from(status) << 16 | u32::from(data1) << 8 | u32::from(data2);
            self.slots[head % SIZE].store(packed, Ordering::Relaxed);
            self.head.store(head.wrapping_add(1), Ordering::Release);
        }
        drop(guard);
    }

    /// Take the next message, audio thread only.
    pub fn pop(&self) -> Option<[u8; 3]> {
        let tail = self.tail.load(Ordering::Relaxed);
        let head = self.head.load(Ordering::Acquire);
        if tail == head {
            return None;
        }
        let packed = self.slots[tail % SIZE].load(Ordering::Relaxed);
        self.tail.store(tail.wrapping_add(1), Ordering::Release);
        Some([(packed >> 16) as u8, (packed >> 8) as u8, packed as u8])
    }
}

impl Default for Ring {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::Ring;

    #[test]
    fn messages_come_out_in_order_and_a_full_ring_drops() {
        let ring = Ring::new();
        assert_eq!(ring.pop(), None);
        ring.push(0x90, 60, 100);
        ring.push(0xC0, 5, 0);
        assert_eq!(ring.pop(), Some([0x90, 60, 100]));
        assert_eq!(ring.pop(), Some([0xC0, 5, 0]));
        assert_eq!(ring.pop(), None);
        for i in 0..300 {
            ring.push(0x90, (i % 128) as u8, 1);
        }
        let mut n = 0;
        while ring.pop().is_some() {
            n += 1;
        }
        assert_eq!(n, 256);
    }
}
