use std::cmp::Ordering;
use std::path::{Path, PathBuf};

const SUPPORTED_EXTENSIONS: &[&str] = &[
    "jpg", "jpeg", "png", "gif", "webp", "bmp", "tiff", "tif", "ico", "apng",
];

pub fn is_supported_image(path: &Path) -> bool {
    path.extension()
        .and_then(|ext| ext.to_str())
        .map(|ext| {
            let lower = ext.to_ascii_lowercase();
            SUPPORTED_EXTENSIONS.iter().any(|&sup| sup == lower)
        })
        .unwrap_or(false)
}

pub fn natural_cmp(a: &str, b: &str) -> Ordering {
    let mut a_chars = a.chars().peekable();
    let mut b_chars = b.chars().peekable();

    loop {
        match (a_chars.peek(), b_chars.peek()) {
            (None, None) => return Ordering::Equal,
            (None, Some(_)) => return Ordering::Less,
            (Some(_), None) => return Ordering::Greater,
            (Some(ca), Some(cb)) => {
                if ca.is_ascii_digit() && cb.is_ascii_digit() {
                    let mut num_a: u64 = 0;
                    while let Some(c) = a_chars.peek() {
                        if c.is_ascii_digit() {
                            num_a = num_a
                                .saturating_mul(10)
                                .saturating_add(c.to_digit(10).unwrap() as u64);
                            a_chars.next();
                        } else {
                            break;
                        }
                    }

                    let mut num_b: u64 = 0;
                    while let Some(c) = b_chars.peek() {
                        if c.is_ascii_digit() {
                            num_b = num_b
                                .saturating_mul(10)
                                .saturating_add(c.to_digit(10).unwrap() as u64);
                            b_chars.next();
                        } else {
                            break;
                        }
                    }

                    let ord = num_a.cmp(&num_b);
                    if ord != Ordering::Equal {
                        return ord;
                    }
                } else {
                    let la = ca.to_ascii_lowercase();
                    let lb = cb.to_ascii_lowercase();
                    let ord = la.cmp(&lb);
                    if ord != Ordering::Equal {
                        return ord;
                    }
                    a_chars.next();
                    b_chars.next();
                }
            }
        }
    }
}

pub fn compare_paths_natural(a: &Path, b: &Path) -> Ordering {
    let str_a = a.file_name().and_then(|n| n.to_str()).unwrap_or("");
    let str_b = b.file_name().and_then(|n| n.to_str()).unwrap_or("");
    let ord = natural_cmp(str_a, str_b);
    if ord != Ordering::Equal {
        ord
    } else {
        a.cmp(b)
    }
}

#[derive(Default)]
pub struct AppState {
    pub images: Vec<PathBuf>,
    pub current_index: Option<usize>,
}

impl AppState {
    pub fn new(initial_path: Option<PathBuf>) -> Self {
        let mut state = Self {
            images: Vec::new(),
            current_index: None,
        };

        if let Some(ref path) = initial_path {
            state.load_path(path);
        }

        state
    }

    pub fn load_path(&mut self, target: &Path) {
        let canonical = match target.canonicalize() {
            Ok(p) => p,
            Err(e) => {
                log::error!("Failed to canonicalize path {:?}: {}", target, e);
                return;
            }
        };

        let (dir, target_file) = if canonical.is_dir() {
            (canonical, None)
        } else if canonical.is_file() {
            let parent = canonical.parent().unwrap_or(Path::new(".")).to_path_buf();
            (parent, Some(canonical))
        } else {
            return;
        };

        let mut collected = Vec::new();
        if let Ok(entries) = std::fs::read_dir(&dir) {
            for entry in entries.flatten() {
                let p = entry.path();
                if p.is_file() && is_supported_image(&p) {
                    collected.push(p);
                }
            }
        }

        collected.sort_by(|a, b| compare_paths_natural(a, b));

        let initial_idx = if let Some(ref file) = target_file {
            collected.iter().position(|p| p == file)
        } else if !collected.is_empty() {
            Some(0)
        } else {
            None
        };

        self.images = collected;
        self.current_index = initial_idx;
    }

    pub fn current_path(&self) -> Option<&Path> {
        self.current_index
            .and_then(|i| self.images.get(i).map(|p| p.as_path()))
    }

    pub fn image_count(&self) -> usize {
        self.images.len()
    }

    pub fn display_index(&self) -> usize {
        self.current_index.map(|i| i + 1).unwrap_or(0)
    }

    #[allow(clippy::should_implement_trait, clippy::collapsible_if)]
    pub fn next(&mut self) -> bool {
        if let Some(idx) = self.current_index {
            if idx + 1 < self.images.len() {
                self.current_index = Some(idx + 1);
                return true;
            }
        }
        false
    }

    #[allow(clippy::collapsible_if)]
    pub fn prev(&mut self) -> bool {
        if let Some(idx) = self.current_index {
            if idx > 0 {
                self.current_index = Some(idx - 1);
                return true;
            }
        }
        false
    }

    pub fn first(&mut self) -> bool {
        if !self.images.is_empty() && self.current_index != Some(0) {
            self.current_index = Some(0);
            return true;
        }
        false
    }

    pub fn last(&mut self) -> bool {
        if !self.images.is_empty() {
            let last_idx = self.images.len() - 1;
            if self.current_index != Some(last_idx) {
                self.current_index = Some(last_idx);
                return true;
            }
        }
        false
    }

    pub fn set_index(&mut self, index: usize) -> bool {
        if index < self.images.len() {
            self.current_index = Some(index);
            return true;
        }
        false
    }

    #[allow(clippy::collapsible_if)]
    pub fn remove_current(&mut self) -> Option<PathBuf> {
        if let Some(idx) = self.current_index {
            if idx < self.images.len() {
                let removed = self.images.remove(idx);
                if self.images.is_empty() {
                    self.current_index = None;
                } else if idx >= self.images.len() {
                    self.current_index = Some(self.images.len() - 1);
                }
                return Some(removed);
            }
        }
        None
    }

    pub fn insert_sorted(&mut self, path: PathBuf) {
        if !self.images.contains(&path) {
            let pos = self
                .images
                .binary_search_by(|probe| compare_paths_natural(probe, &path))
                .unwrap_or_else(|i| i);
            self.images.insert(pos, path);
            if let Some(idx) = self.current_index {
                if pos <= idx {
                    self.current_index = Some(idx + 1);
                }
            } else {
                self.current_index = Some(0);
            }
        }
    }

    pub fn remove_path(&mut self, path: &Path) -> bool {
        if let Some(pos) = self.images.iter().position(|p| p == path) {
            self.images.remove(pos);
            if self.images.is_empty() {
                self.current_index = None;
            } else if let Some(idx) = self.current_index {
                if pos < idx {
                    self.current_index = Some(idx - 1);
                } else if pos == idx && idx >= self.images.len() {
                    self.current_index = Some(self.images.len() - 1);
                }
            }
            return true;
        }
        false
    }

    pub fn rename_path(&mut self, from: &Path, to: PathBuf) {
        if let Some(pos) = self.images.iter().position(|p| p == from) {
            let current_image_path = self.current_path().map(|p| p.to_path_buf());
            if is_supported_image(&to) {
                self.images[pos] = to.clone();
                self.images.sort_by(|a, b| compare_paths_natural(a, b));
                if let Some(ref curr) = current_image_path {
                    let target = if curr == from { &to } else { curr };
                    self.current_index = self.images.iter().position(|p| p == target);
                }
            } else {
                self.remove_path(from);
            }
        }
    }

    #[allow(clippy::collapsible_if)]
    pub fn rename_current(&mut self, new_path: PathBuf) {
        if let Some(idx) = self.current_index {
            if idx < self.images.len() {
                if is_supported_image(&new_path) {
                    self.images[idx] = new_path.clone();
                    self.images.sort_by(|a, b| compare_paths_natural(a, b));
                    self.current_index = self.images.iter().position(|p| p == &new_path);
                } else {
                    self.remove_current();
                }
            }
        }
    }
}
