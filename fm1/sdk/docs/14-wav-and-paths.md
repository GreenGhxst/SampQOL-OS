# WAV files and paths

The SDK has a small WAV reader and the path rules the app uses. These are the
fallbacks when a sample is streamed instead of held in RAM.

## Read a WAV header

```rust
use mpc_sample_sdk::platform::wav;

let path = std::path::Path::new(
    "/data/TempProjects/Temp-Project_[ProjectData]/kick.wav",
);
if let Some(info) = wav::Info::read(path) {
    mpc_sample_sdk::platform::status::set_status(&format!(
        "{} frames, {} ch, {} Hz, {} bytes/sample",
        info.frames, info.channels, info.rate, info.width
    ));
}
```

## Read samples as f32

```rust
use mpc_sample_sdk::platform::wav;

let path = std::path::Path::new(
    "/data/TempProjects/Temp-Project_[ProjectData]/kick.wav",
);
let audio = wav::read_f32(path, 10_501, 2, 44_100);
if let Some(samples) = audio {
    assert_eq!(samples.len(), 10_501 * 2);
}
```

Pass the geometry you expect. `read_f32` returns `None` when the header, length
or format does not match; it does not guess.

## Find the WAV for a sample

```rust
use mpc_sample_sdk::platform::{paths, sample};

fn sample_path(sample_object: usize) -> Option<std::path::PathBuf> {
    let frames = sample::frames(sample_object);
    paths::for_sample(sample_object, frames, 2, 44_100)
}
```

`paths::for_sample` first uses the sample's own object graph and then the
project's path index. `paths::from_object` is the object-only variant.

For a newly written file:

```rust
use mpc_sample_sdk::platform::paths;

let root = std::path::Path::new(paths::TEMP_PROJECTS);
if let Some(path) = paths::unique(root, 44_100, 2, 44_100) {
    mpc_sample_sdk::platform::status::set_status(&format!("{}", path.display()));
}
```

`unique` creates a path that does not collide with an existing sample. The SDK
does not write the WAV for you; use `extract` for pads or write your own file
under the same root.

## Reading text paths

```rust
use mpc_sample_sdk::platform::paths;

let project_text = b"/data/TempProjects/My Project_[ProjectData]/kick.wav\0";
if let Some(path) = paths::in_text(project_text, 10_501) {
    mpc_sample_sdk::platform::status::set_status(&format!("{}", path.display()));
}
```

`in_text` searches a NUL-terminated project path blob; `in_text_under` restricts
the search to given roots.
