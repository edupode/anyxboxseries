use std::path::PathBuf;
use victory_core::{PolicyError, ensure_outside_repo};

fn main() -> anyhow::Result<()> {
    let args: Vec<_> = std::env::args_os().skip(1).map(PathBuf::from).collect();
    anyhow::ensure!(
        args.len() == 2,
        "expected a protected and an external directory"
    );
    let protected = &args[0];
    let external = &args[1];
    anyhow::ensure!(
        protected.join(".victory-root").is_file() && external.is_dir(),
        "missing fixtures"
    );
    for path in [
        protected.clone(),
        protected.join("nested"),
        protected.join("uncreated").join("output"),
    ] {
        anyhow::ensure!(
            matches!(
                ensure_outside_repo(&path),
                Err(PolicyError::InsideRepo { .. })
            ),
            "protected check failed for {path:?}"
        );
    }
    for path in [
        external.clone(),
        external.join("sample.txt"),
        external.join("uncreated").join("images"),
    ] {
        ensure_outside_repo(&path)?;
    }
    anyhow::ensure!(
        external.join("sample.txt").is_file() && !external.join("uncreated").exists(),
        "external fixture changed"
    );
    println!(
        "ordinary canonicalize on external directory: {:?}",
        std::fs::canonicalize(external)
    );
    println!(
        "POLICY CHECK PASSED: three protected and three external paths, including nonexistent descendants"
    );
    Ok(())
}
