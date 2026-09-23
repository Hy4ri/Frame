use gpui_kit::RenderImage;
use parking_lot::Mutex;
use std::collections::{HashMap, VecDeque};
use std::path::{Path, PathBuf};
use std::sync::Arc;

struct CacheEntry {
    image: Arc<RenderImage>,
    byte_size: usize,
}

pub struct ImageCache {
    max_entries: usize,
    max_bytes: usize,
    inner: Mutex<CacheInner>,
}

struct CacheInner {
    entries: HashMap<PathBuf, CacheEntry>,
    order: VecDeque<PathBuf>,
    total_bytes: usize,
    pinned_path: Option<PathBuf>,
}

impl ImageCache {
    pub fn new(max_entries: usize, max_bytes: usize) -> Self {
        Self {
            max_entries,
            max_bytes,
            inner: Mutex::new(CacheInner {
                entries: HashMap::new(),
                order: VecDeque::new(),
                total_bytes: 0,
                pinned_path: None,
            }),
        }
    }

    pub fn get(&self, path: &Path) -> Option<Arc<RenderImage>> {
        let mut inner = self.inner.lock();
        if let Some(entry) = inner.entries.get(path) {
            let img = entry.image.clone();
            let p_buf = path.to_path_buf();
            inner.order.retain(|p| p != &p_buf);
            inner.order.push_back(p_buf);
            Some(img)
        } else {
            None
        }
    }

    pub fn put(&self, path: PathBuf, image: Arc<RenderImage>, byte_size: usize) {
        let mut inner = self.inner.lock();

        if inner.entries.contains_key(&path) {
            return;
        }

        while inner.entries.len() >= self.max_entries
            || (self.max_bytes > 0 && inner.total_bytes + byte_size > self.max_bytes)
        {
            let mut evicted = false;
            for i in 0..inner.order.len() {
                let candidate = &inner.order[i];
                if Some(candidate) == inner.pinned_path.as_ref() {
                    continue;
                }
                let removed_path = inner.order.remove(i).unwrap();
                if let Some(removed_entry) = inner.entries.remove(&removed_path) {
                    inner.total_bytes = inner.total_bytes.saturating_sub(removed_entry.byte_size);
                }
                evicted = true;
                break;
            }
            if !evicted {
                break;
            }
        }

        inner.total_bytes += byte_size;
        inner.entries.insert(
            path.clone(),
            CacheEntry {
                image,
                byte_size,
            },
        );
        inner.order.push_back(path);
    }

    pub fn pin(&self, path: Option<&Path>) {
        let mut inner = self.inner.lock();
        inner.pinned_path = path.map(|p| p.to_path_buf());
    }

    pub fn remove(&self, path: &Path) {
        let mut inner = self.inner.lock();
        if let Some(entry) = inner.entries.remove(path) {
            inner.total_bytes = inner.total_bytes.saturating_sub(entry.byte_size);
            inner.order.retain(|p| p != path);
        }
    }

    pub fn rename(&self, old_path: &Path, new_path: PathBuf) {
        let mut inner = self.inner.lock();
        if let Some(entry) = inner.entries.remove(old_path) {
            inner.order.retain(|p| p != old_path);
            inner.order.push_back(new_path.clone());
            inner.entries.insert(new_path, entry);
        }
    }
}
