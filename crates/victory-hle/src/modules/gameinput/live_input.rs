use super::{KeyState, PADS, PadState, Reading, Value, fallback};
use std::fs::{self, File};
use std::io::Read;
use std::os::windows::fs::{MetadataExt, OpenOptionsExt};
use std::path::{Component, Path, PathBuf, Prefix};
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::{Duration, Instant};

struct Command {
    sequence: u64,
    key: u8,
    duration: Duration,
    /// A compound command's second key, and how long after the first it goes down.
    second: Option<(u8, Duration)>,
}

/// Shared only by immutable readings generated during this private command phase.
/// A getter may read a current or historical object, even after the next command starts.
pub(super) struct Phase {
    sequence: u64,
    key: u8,
    down: bool,
    /// A compound command's second key, and whether it is down in this phase.
    second: Option<(u8, bool)>,
    pad_reported: AtomicBool,
    keyboard_reported: AtomicBool,
}

impl Phase {
    fn new(sequence: u64, key: u8, down: bool, second: Option<(u8, bool)>) -> Arc<Self> {
        Arc::new(Self {
            sequence,
            key,
            down,
            second,
            pad_reported: false.into(),
            keyboard_reported: false.into(),
        })
    }

    fn report(&self, consumer: &'static str, reading_sequence: u64, already: &AtomicBool) {
        if !already.swap(true, Ordering::Relaxed) {
            // `second_pressed` is recorded for compound commands only.
            tracing::info!(
                command_sequence = self.sequence, pressed = self.down,
                second_pressed = self.second.map(|(_, down)| down), reading_sequence, consumer,
                "private input returned to caller"
            );
        }
    }

    fn active(key: u8, value: &Value) -> bool {
        let pad = match value {
            Value::Keys(keys) => return keys.iter().any(|k| k.virtual_key == key),
            Value::Pad(pad) => pad,
        };
        let component = fallback(&[KeyState {
            virtual_key: key,
            ..Default::default()
        }]);
        pad.buttons & component.buttons != 0
            || pad.left_x * component.left_x > 0.
            || pad.left_y * component.left_y > 0.
            || pad.right_x * component.right_x > 0.
            || pad.right_y * component.right_y > 0.
            || pad.left_trigger * component.left_trigger > 0.
            || pad.right_trigger * component.right_trigger > 0.
    }

    /// `value` has every key of the command in this phase's state: a second key
    /// that is not due yet must be absent, or its later edge could not be told apart.
    fn shown(&self, value: &Value) -> bool {
        Self::active(self.key, value) == self.down
            && self.second.is_none_or(|(second, down)| Self::active(second, value) == down)
    }

    pub(super) fn follows(&self, prior: &Reading, new: &Reading) -> bool {
        let held = prior.private_phase.get().is_some_and(|p| p.sequence == self.sequence && p.down);
        // The key this phase pressed last: the second one once it is down.
        let edge = match self.second {
            Some((second, true)) => second,
            _ => self.key,
        };
        // A physical control already held before the private pulse gives no evidence of that pulse.
        self.shown(&new.value) && (held || (self.down && !Self::active(edge, &prior.value)))
    }

    pub(super) fn pad_returned(&self, reading_sequence: u64) {
        self.report("GetGamepadState", reading_sequence, &self.pad_reported);
    }

    pub(super) fn keys_returned(&self, consumer: &'static str, reading_sequence: u64, keys: &[KeyState], n: usize) {
        let proven = |(key, down): (u8, bool)| {
            let copied = keys[..n].iter().any(|k| k.virtual_key == key);
            // A truncated array can prove presence, but cannot prove absence.
            copied == down && (copied || n == keys.len())
        };
        if proven((self.key, self.down)) && self.second.is_none_or(proven) {
            self.report(consumer, reading_sequence, &self.keyboard_reported);
        }
    }
}

fn named_key(name: &str) -> Option<u8> {
    Some(match name {
        "Enter" | "PadA" => 0x0d,
        "Back" | "PadB" => 0x08,
        "Esc" | "Menu" => 0x1b,
        "Tab" | "View" => 0x09,
        "Up" => 0x26,
        "Down" => 0x28,
        "Left" => 0x25,
        "Right" => 0x27,
        s if s.len() == 1 && b"WASDIJKLZXCYQEFH".contains(&s.as_bytes()[0]) => s.as_bytes()[0],
        _ => return None,
    })
}

/// `sequence key duration_ms`, or `sequence key+key duration_ms delay_ms`: the first key is
/// down for the whole duration, the second from the delay on, and both come up together.
fn parse(bytes: &[u8]) -> Option<Command> {
    if bytes.len() > 96 || !bytes.is_ascii() {
        return None;
    }
    let mut parts = std::str::from_utf8(bytes).ok()?.split_ascii_whitespace();
    let sequence = parts.next()?.parse().ok()?;
    let keys = parts.next()?;
    let (key, second) = match keys.split_once('+') {
        Some((first, second)) => (named_key(first)?, Some(named_key(second)?)),
        None => (named_key(keys)?, None),
    };
    let ms = parts.next()?.parse::<u64>().ok()?;
    // The delay field belongs to a second key: required with one, an extra field without.
    let second = match second {
        Some(second) => Some((second, parts.next()?.parse::<u64>().ok()?)),
        None => None,
    };
    if sequence == 0
        || !(40..=10_000).contains(&ms)
        || second.is_some_and(|(second, delay)| second == key || delay == 0 || delay >= ms)
        || parts.next().is_some()
    {
        return None;
    }
    Some(Command {
        sequence,
        key,
        duration: Duration::from_millis(ms),
        second: second.map(|(second, delay)| (second, Duration::from_millis(delay))),
    })
}

pub(super) struct Channel {
    file: Option<PathBuf>,
    sequence: u64,
    next_poll: Instant,
    active: Option<(u8, Instant)>,
    /// The active compound command's second key and when it is due, until a poll takes it down.
    delayed: Option<(u8, Instant)>,
    /// That key once `held` has taken it down; none again when `held` releases.
    pub(super) second: Option<u8>,
    pub(super) phase: Option<Arc<Phase>>,
    traced_commands: u16,
}

impl Default for Channel {
    fn default() -> Self {
        let root = std::env::var_os("VICTORY_LIVE_INPUT_DIR").map(PathBuf::from);
        Self::new(root.as_deref(), Instant::now())
    }
}

impl Channel {
    fn new(root: Option<&Path>, now: Instant) -> Self {
        let file = root.and_then(|dir| {
            let mut parts = dir.components();
            if !matches!(parts.next(),Some(Component::Prefix(p)) if matches!(p.kind(),Prefix::Disk(_)))
                || !matches!(parts.next(),Some(Component::RootDir))
                || !parts.all(|part| matches!(part, Component::Normal(_))) { return None; }
            let meta = fs::symlink_metadata(dir).ok()?;
            (meta.is_dir() && meta.file_attributes() & 0x400 == 0).then(|| dir.join("input.txt"))
        });
        let mut state = Self {
            file,
            sequence: 0,
            next_poll: now,
            active: None,
            delayed: None,
            second: None,
            phase: None,
            traced_commands: 0,
        };
        // A command left in place by a previous process must never trigger.
        if let Some(command) = state.read() {
            state.sequence = command.sequence;
        }
        if state.file.is_some() {
            tracing::info!(sequence = state.sequence, "live input ready");
        }
        state
    }

    fn read(&self) -> Option<Command> {
        let path = self.file.as_deref()?;
        // Never follow a command-file reparse point. The configured directory
        // is supplied by the launcher, host-owned and read-only to the guest.
        let file = File::options()
            .read(true)
            .custom_flags(0x0020_0000)
            .open(path)
            .ok()?;
        let meta = file.metadata().ok()?;
        if !meta.is_file() || meta.file_attributes() & 0x400 != 0 || meta.len() > 96 {
            return None;
        }
        let mut bytes = Vec::with_capacity(97);
        file.take(97).read_to_end(&mut bytes).ok()?;
        parse(&bytes)
    }

    pub(super) fn held(&mut self, now: Instant) -> Option<u8> {
        self.file.as_ref()?;
        if self.phase.as_ref().is_some_and(|phase| !phase.down) {
            self.phase = None;
        }
        if let Some((key, until)) = self.active {
            if now < until {
                if let Some((second, due)) = self.delayed
                    && now >= due
                {
                    // The edge belongs to this sample, however late the poll; it is never back-dated.
                    self.delayed = None;
                    self.second = Some(second);
                    if self.phase.is_some() {
                        self.phase = Some(Phase::new(self.sequence, key, true, Some((second, true))));
                    }
                    tracing::info!(
                        sequence = self.sequence,
                        vk = second,
                        late_ms = (now - due).as_millis() as u64,
                        "live input second key pressed"
                    );
                }
                return Some(key);
            }
            self.active = None;
            // Polls that skipped the second key's whole window never sampled it down: the
            // command ends from its first-key phase, and no phase says the key was pressed.
            let missed = self.delayed.take().map(|(second, _)| second);
            let second = self.second.take().or(missed);
            if self.phase.is_some() {
                self.phase = Some(Phase::new(self.sequence, key, false, second.map(|second| (second, false))));
            }
            tracing::info!(sequence = self.sequence, vk = key, "live input released");
            if let Some(second) = missed {
                tracing::warn!(sequence = self.sequence, vk = second, "live input second key never sampled");
            }
            // Publish at least one released sample before another command.
            return None;
        }
        if now < self.next_poll {
            return None;
        }
        self.next_poll = now + Duration::from_millis(32);
        let command = self.read()?;
        if command.sequence <= self.sequence {
            return None;
        }
        self.sequence = command.sequence;
        self.active = Some((command.key, now + command.duration));
        self.delayed = command.second.map(|(second, delay)| (second, now + delay));
        // At most two consumers for each phase of the first 128 private commands.
        if self.traced_commands < 128 {
            self.traced_commands += 1;
            let second = command.second.map(|(second, _)| (second, false));
            self.phase = Some(Phase::new(command.sequence, command.key, true, second));
        }
        // The second key's fields are recorded for compound commands only.
        tracing::info!(
            sequence = self.sequence,
            vk = command.key,
            hold_ms = command.duration.as_millis() as u64,
            second_vk = command.second.map(|(second, _)| second),
            delay_ms = command.second.map(|(_, delay)| delay.as_millis() as u64),
            "live input pressed"
        );
        Some(command.key)
    }
}

type Sample = ([Option<PadState>; PADS], Vec<KeyState>);

pub(super) fn merge(mut sample: Sample, keys: [Option<KeyState>; 2]) -> Sample {
    let mut keys = keys.into_iter().flatten();
    let Some(first) = keys.next() else { return sample };
    let second = keys.next();
    let pressed = [first, second.unwrap_or_default()];
    let pressed = &pressed[..1 + usize::from(second.is_some())];
    for key in pressed {
        if !sample.1.iter().any(|k| k.virtual_key == key.virtual_key) {
            sample.1.push(*key);
        }
    }
    // The regular keyboard stand-in handles the empty-XInput case. With a
    // real controller, combine the command with player one's current sample.
    if let Some(player) = sample.0.iter_mut().flatten().next() {
        let overlay = fallback(pressed);
        player.buttons |= overlay.buttons;
        player.left_trigger = player.left_trigger.max(overlay.left_trigger);
        player.right_trigger = player.right_trigger.max(overlay.right_trigger);
        player.left_x = (player.left_x + overlay.left_x).clamp(-1., 1.);
        player.left_y = (player.left_y + overlay.left_y).clamp(-1., 1.);
        player.right_x = (player.right_x + overlay.right_x).clamp(-1., 1.);
        player.right_y = (player.right_y + overlay.right_y).clamp(-1., 1.);
    }
    sample
}

#[cfg(test)]
mod tests {
    use super::*;
    use super::super::{FALLBACK, GAMEPAD_KIND, Hub, KEYBOARD, KEYBOARD_KIND, Reading, S_OK, Source,
                      current_reading, gamepad_state, key_count, key_state, reading, reading_release};
    use std::sync::Mutex;

    /// No host pad, a physical Q held throughout, and a private channel on the test's clock.
    struct Synthetic {
        clock: Arc<Mutex<Instant>>,
        channel: Channel,
    }
    impl Source for Synthetic {
        fn pad(&mut self, _: usize) -> Option<PadState> { None }
        fn keys(&mut self) -> Vec<KeyState> {
            vec![KeyState { virtual_key: b'Q', ..Default::default() }]
        }
        fn live_keys(&mut self) -> ([Option<KeyState>; 2], Option<Arc<Phase>>) {
            let first = self.channel.held(*self.clock.lock().unwrap());
            let keys = [first, self.channel.second]
                .map(|key| key.map(|virtual_key| KeyState { virtual_key, ..Default::default() }));
            (keys, self.channel.phase.clone())
        }
        fn rumble(&mut self, _: usize, _: u16, _: u16) {}
    }

    #[test]
    fn private_command_is_observed_only_when_matching_state_is_returned() {
        let tmp = tempfile::tempdir().unwrap();
        let path = tmp.path().join("input.txt");
        fs::write(&path, "1 Back 100").unwrap();
        let start = Instant::now();
        let clock = Arc::new(Mutex::new(start));
        let hub = Hub::new(Box::new(Synthetic {
            channel: Channel::new(Some(tmp.path()), start), clock: Arc::clone(&clock),
        }));
        let current = |kind, device: usize| -> *const Reading {
            let mut out = std::ptr::null();
            assert_eq!(current_reading(&hub.object, kind, &hub.devices[device], &mut out), S_OK);
            out.cast()
        };
        hub.poll();
        let neutral = current(GAMEPAD_KIND, FALLBACK);
        assert!(reading(neutral).private_phase.get().is_none()); // Stale initial file is never a tagged pulse.
        fs::write(&path, "2 L 100").unwrap();
        *clock.lock().unwrap() = start + Duration::from_millis(40);
        hub.poll();
        let pad_down = current(GAMEPAD_KIND, FALLBACK);
        let key_down = current(KEYBOARD_KIND, KEYBOARD);
        let press = reading(pad_down).private_phase.get().unwrap();
        assert!(Arc::ptr_eq(press, reading(key_down).private_phase.get().unwrap()));
        assert!(!press.pad_reported.load(Ordering::Relaxed)); // Polling and getting a reading do not prove use.
        let mut state = PadState::default();
        assert!(!gamepad_state(pad_down, std::ptr::null_mut()));
        assert!(!gamepad_state(key_down, &mut state));
        assert_eq!(key_count(key_down), 2);
        let mut keys = [KeyState::default(); 2];
        assert_eq!(key_state(key_down, 1, keys.as_mut_ptr()), 1); // Only the fixture's Q was copied.
        assert!(!press.keyboard_reported.load(Ordering::Relaxed));
        assert!(!press.pad_reported.load(Ordering::Relaxed));
        assert_eq!(key_state(key_down, 2, keys.as_mut_ptr()), 2);
        assert_eq!(keys[1].virtual_key, b'L');
        assert!(press.keyboard_reported.load(Ordering::Relaxed));

        *clock.lock().unwrap() = start + Duration::from_millis(145);
        hub.poll();
        let pad_up = current(GAMEPAD_KIND, FALLBACK);
        let key_up = current(KEYBOARD_KIND, KEYBOARD);
        let release = reading(pad_up).private_phase.get().unwrap();
        assert_eq!(release.sequence, press.sequence);
        assert!(!release.down);
        assert!(gamepad_state(pad_down, &mut state)); // Earlier immutable reading still denotes the press.
        assert_eq!(state.right_x, 1.);
        assert!(press.pad_reported.load(Ordering::Relaxed));
        assert!(!release.pad_reported.load(Ordering::Relaxed));
        assert!(gamepad_state(pad_up, &mut state));
        assert_eq!(state.right_x, 0.);
        assert!(release.pad_reported.load(Ordering::Relaxed));
        assert_eq!(key_state(key_up, 0, keys.as_mut_ptr()), 0);
        assert!(!release.keyboard_reported.load(Ordering::Relaxed));
        assert_eq!(key_state(key_up, 2, keys.as_mut_ptr()), 1);
        assert_eq!(keys[0].virtual_key, b'Q');
        assert!(release.keyboard_reported.load(Ordering::Relaxed));
        // Q is already held by the synthetic hardware. The command creates no new pad or keyboard reading.
        fs::write(&path, "3 Q 100").unwrap();
        *clock.lock().unwrap() = start + Duration::from_millis(193);
        hub.poll();
        let existing = current(GAMEPAD_KIND, FALLBACK);
        assert_eq!(existing, pad_up);
        assert_eq!(reading(existing).private_phase.get().unwrap().sequence, 2);
        for obj in [neutral, pad_down, key_down, pad_up, key_up, existing] { reading_release(obj); }
    }

    #[test]
    fn live_input_sequences_releases_and_preserves_physical_controls() {
        let tmp = tempfile::tempdir().unwrap();
        let file = tmp.path().join("input.txt");
        fs::write(&file, "51 Enter 200").unwrap();
        let start = Instant::now();
        let at = |ms| start + Duration::from_millis(ms);
        let mut live = Channel::new(Some(tmp.path()), start);
        assert_eq!(live.held(at(0)), None);
        for bad in [
            "52 ../../x 200",
            "52 Enter 0",
            "52 Enter 10001",
            "52 Enter 100 extra",
        ] {
            assert!(parse(bad.as_bytes()).is_none());
        }
        assert!(parse(&[b'X'; 97]).is_none());
        fs::write(&file, "52 W 200").unwrap();
        assert_eq!(live.held(at(40)), Some(b'W'));
        fs::write(&file, "53 Enter 200").unwrap();
        assert_eq!(live.held(at(239)), Some(b'W'));
        assert_eq!(live.held(at(240)), None);
        assert_eq!(live.held(at(248)), Some(0x0d));
        assert_eq!(live.held(at(448)), None);
        fs::write(&file, "51 Enter 999").unwrap();
        assert_eq!(live.held(at(500)), None);
        let physical = PadState {
            buttons: super::super::B,
            right_trigger: 0.4,
            ..Default::default()
        };
        let keys = vec![KeyState {
            virtual_key: b'Z',
            ..Default::default()
        }];
        let sample = ([None, Some(physical), Some(physical), None], keys.clone());
        let (pads, keyboard) = merge(
            sample.clone(),
            [Some(KeyState {
                virtual_key: 0x0d,
                ..Default::default()
            }), None],
        );
        assert_eq!(pads[1].unwrap().buttons, super::super::B | super::super::A);
        assert_eq!(pads[1].unwrap().right_trigger, 0.4);
        assert_eq!(pads[2], Some(physical));
        assert_eq!(keyboard.len(), 2);
        assert_eq!(merge(sample.clone(), [None, None]), sample);
        let mut moving = sample.clone();
        moving.0[1].as_mut().unwrap().left_x = 0.7;
        let opposites = [b'A', b'D'].map(|virtual_key| Some(KeyState { virtual_key, ..Default::default() }));
        assert_eq!(merge(moving, opposites).0[1].unwrap().left_x, 0.7);
    }

    #[test]
    fn delayed_second_key_is_reported_only_from_readings_that_return_it() {
        for bad in [
            "7 Z+Z 200 80",        // One key twice,
            "7 Enter+PadA 200 80", // also under two names.
            "7 Z+PadA+X 200 80",
            "7 Z+G 200 80",
            "7 Z+PadA 200",
            "7 Z+PadA 200 0",
            "7 Z+PadA 200 200",
            "7 Z+PadA 10001 80",
            "7 Z+PadA 200 80 1",
            "7 Z 200 80",
        ] {
            assert!(parse(bad.as_bytes()).is_none(), "{bad}");
        }
        let tmp = tempfile::tempdir().unwrap();
        let path = tmp.path().join("input.txt");
        let start = Instant::now();
        let clock = Arc::new(Mutex::new(start));
        let hub = Hub::new(Box::new(Synthetic {
            channel: Channel::new(Some(tmp.path()), start), clock: Arc::clone(&clock),
        }));
        let poll_at = |ms| {
            *clock.lock().unwrap() = start + Duration::from_millis(ms);
            hub.poll();
        };
        let current = |kind, device: usize| -> *const Reading {
            let mut out = std::ptr::null();
            assert_eq!(current_reading(&hub.object, kind, &hub.devices[device], &mut out), S_OK);
            out.cast()
        };
        // What GetGamepadState really hands back for a reading.
        let pad = |r: *const Reading| {
            let mut state = PadState::default();
            assert!(gamepad_state(r, &mut state));
            (state.left_trigger, state.buttons)
        };
        let lb = super::super::LEFT_SHOULDER; // The fixture's physical Q, kept in every sample.

        poll_at(0);
        fs::write(&path, "7 Z+PadA 200 80").unwrap();
        poll_at(40);
        let lt = current(GAMEPAD_KIND, FALLBACK);
        let lt_keys = current(KEYBOARD_KIND, KEYBOARD);
        poll_at(100); // 60 ms in: A is not due, and nothing new is published.
        let still_lt = current(GAMEPAD_KIND, FALLBACK);
        assert_eq!(still_lt, lt);
        poll_at(120);
        let chord = current(GAMEPAD_KIND, FALLBACK);
        let chord_keys = current(KEYBOARD_KIND, KEYBOARD);
        poll_at(240);
        let up = current(GAMEPAD_KIND, FALLBACK);
        let first = reading(lt).private_phase.get().unwrap();
        let second = reading(chord).private_phase.get().unwrap();
        let last = reading(up).private_phase.get().unwrap();
        assert!(Arc::ptr_eq(first, reading(lt_keys).private_phase.get().unwrap()));
        assert!(Arc::ptr_eq(second, reading(chord_keys).private_phase.get().unwrap()));
        assert_eq!((first.sequence, first.down, first.second), (7, true, Some((0x0d, false))));
        assert_eq!((second.sequence, second.down, second.second), (7, true, Some((0x0d, true))));
        assert_eq!((last.sequence, last.down, last.second), (7, false, Some((0x0d, false))));

        // Publishing proves nothing: each phase is reported when its own state is returned.
        assert!(!first.pad_reported.load(Ordering::Relaxed));
        assert_eq!(pad(lt), (1., lb)); // The earlier immutable reading still denotes LT alone.
        assert!(first.pad_reported.load(Ordering::Relaxed));
        assert!(!second.pad_reported.load(Ordering::Relaxed));
        assert_eq!(pad(chord), (1., lb | super::super::A));
        assert!(second.pad_reported.load(Ordering::Relaxed));
        assert!(!last.pad_reported.load(Ordering::Relaxed));
        assert_eq!(pad(up), (0., lb));
        assert!(last.pad_reported.load(Ordering::Relaxed));
        let mut keys = [KeyState::default(); 3];
        assert_eq!(key_state(chord_keys, 2, keys.as_mut_ptr()), 2); // Q and Z: truncated before Enter.
        assert!(!second.keyboard_reported.load(Ordering::Relaxed));
        assert_eq!(key_state(chord_keys, 3, keys.as_mut_ptr()), 3);
        assert_eq!(keys.map(|k| k.virtual_key), [b'Q', b'Z', 0x0d]);
        assert!(second.keyboard_reported.load(Ordering::Relaxed));
        assert_eq!(key_state(lt_keys, 3, keys.as_mut_ptr()), 2); // Complete: Z, and provably no Enter.
        assert!(first.keyboard_reported.load(Ordering::Relaxed));

        // The next command's whole LT+A window falls between two polls: no such reading, no claim.
        fs::write(&path, "8 Z+PadA 200 80").unwrap();
        poll_at(248);
        let lt_again = current(GAMEPAD_KIND, FALLBACK);
        poll_at(448);
        let dropped = current(GAMEPAD_KIND, FALLBACK);
        assert_eq!(reading(dropped).sequence, reading(lt_again).sequence + 1);
        let held = reading(lt_again).private_phase.get().unwrap();
        let released = reading(dropped).private_phase.get().unwrap();
        assert_eq!((held.sequence, held.down, held.second), (8, true, Some((0x0d, false))));
        assert_eq!((released.sequence, released.down, released.second), (8, false, Some((0x0d, false))));
        assert_eq!((pad(lt_again), pad(dropped)), ((1., lb), (0., lb)));
        for obj in [lt, lt_keys, still_lt, chord, chord_keys, up, lt_again, dropped] { reading_release(obj); }
    }
}
