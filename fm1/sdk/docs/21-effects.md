# Firmware effects

The SDK can run the app's own reverb, vinyl simulator and decimator on a whole
sample. This is the firmware's sound, not a reimplementation.

## By effect code

```rust
use mpc_sample_sdk::platform::effect;

let input = vec![0.0_f32; 10_501 * 2];
let code = 0; // 0 = reverb, 1 = vinyl, 2 = decimator
let parameters = &[]; // no parameter overrides
if let Some(audio) = effect::render_code(code, &input, 2, 44_100, parameters) {
    mpc_sample_sdk::platform::status::set_status(&format!("{} samples", audio.len()));
}
```

`render_code` returns `None` for an unknown code, an unsupported buffer/rate, or
an image that cannot create the effect.

## By enum, with the tail length

```rust
use mpc_sample_sdk::platform::effect::{self, Effect};

let input = vec![0.0_f32; 10_501 * 2];
match effect::render(Effect::Reverb, &input, 2, 44_100) {
    Ok(rendered) => {
        mpc_sample_sdk::platform::status::set_status(&format!(
            "{} frames, {} samples",
            rendered.frames,
            rendered.audio.len()
        ));
    }
    Err(error) => {
        mpc_sample_sdk::platform::status::set_status(&format!("effect: {error}"));
    }
}
```

`render` returns `Rendered { audio, frames }`; `frames` is the usable length after
the tail was trimmed.

## Parameter overrides

`parameters` is a list of `(index, value)` pairs applied to the effect's own knob
array. The indices belong to the effect, not to the SDK:

```rust
use mpc_sample_sdk::platform::effect;

let input = vec![0.0_f32; 10_501 * 2];
let vinyl = effect::render_code(
    1,
    &input,
    2,
    44_100,
    &[(0, 0.18), (5, 0.12)],
);
if vinyl.is_some() {
    mpc_sample_sdk::platform::status::set_status("vinyl rendered");
}
```

Put feature-specific values in the feature, not in the SDK.

## Errors

`EffectError` covers empty/ragged buffers, channels other than mono/stereo, an
unsupported rate, a refused firmware factory, and non-finite output. Do not
unwrap it on user audio; report the error instead.
