use anyhow::Result;
use std::path::{Path, PathBuf};

pub fn trash_file(path: &Path) -> Result<()> {
    trash::delete(path)?;
    Ok(())
}

pub fn rename_file(old_path: &Path, new_name: &str) -> Result<PathBuf> {
    if new_name.is_empty() || new_name.contains('/') || new_name == "." || new_name == ".." {
        anyhow::bail!("Invalid filename");
    }

    let parent = old_path.parent().unwrap_or(Path::new("."));
    let new_path = parent.join(new_name);

    if new_path.exists() {
        anyhow::bail!("Destination file already exists");
    }

    std::fs::rename(old_path, &new_path)?;
    Ok(new_path)
}
