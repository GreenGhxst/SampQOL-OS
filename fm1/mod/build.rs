// SPDX-License-Identifier: GPL-3.0-only
//! Compiles the open FM1 DX7 engine (C) and the bank module into the mod.
use std::path::PathBuf;

fn main() {
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("..");
    let eng = root.join("engine/fm1_open");
    let synth = eng.join("web/synth");
    let mut build = cc::Build::new();
    for file in [
        "dx7core.c",
        "dx7env.c",
        "dx7lfo.c",
        "dx7math.c",
        "dx7note.c",
        "dx7pitchenv.c",
        "dx7synth.c",
        "adsr.c",
        "fixed.c",
    ] {
        build.file(synth.join(file));
    }
    build.file(root.join("shim/fm1_bank.c"));
    build
        .include(&synth)
        .include(eng.join("third_party/synth"))
        .include(eng.join("sdk/include"))
        .include(eng.join("app"))
        .include(root.join("shim"))
        .define("FM1_HOST_TEST", None)
        .define("FM1_OPEN_WEB", None)
        .flag_if_supported("-std=gnu11")
        .flag_if_supported("-fno-stack-protector")
        .opt_level(2)
        .pic(true)
        .warnings(false)
        .compile("fm1engine");
    println!("cargo:rerun-if-changed={}", synth.display());
    println!(
        "cargo:rerun-if-changed={}",
        root.join("shim/fm1_bank.c").display()
    );
}
