use std::process::Command;

fn main() {
    // Build yupiter library if not already built
    let output = Command::new("make")
        .arg("-C")
        .arg("..")
        .output()
        .expect("failed to run make");

    if !output.status.success() {
        panic!(
            "make failed: {}",
            String::from_utf8_lossy(&output.stderr)
        );
    }

    let build_dir = std::path::PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .unwrap()
        .join("build");

    println!("cargo:rustc-link-search=native={}", build_dir.display());
    println!("cargo:rustc-link-lib=dylib=yupiter");
}