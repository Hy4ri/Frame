use crate::cache::ImageCache;
use crate::loader::{calc_image_bytes, create_thumbnail, load_image};
use gpui::Context;
use parking_lot::Mutex;
use rayon::prelude::*;
use std::collections::HashSet;
use std::path::PathBuf;
use std::sync::Arc;
use std::sync::atomic::{AtomicU64, Ordering};

#[derive(Clone)]
pub struct Prefetcher {
    image_cache: Arc<ImageCache>,
    thumb_cache: Arc<ImageCache>,
    in_flight: Arc<Mutex<HashSet<PathBuf>>>,
    generation: Arc<AtomicU64>,
}

impl Prefetcher {
    pub fn new(image_cache: Arc<ImageCache>, thumb_cache: Arc<ImageCache>) -> Self {
        Self {
            image_cache,
            thumb_cache,
            in_flight: Arc::new(Mutex::new(HashSet::new())),
            generation: Arc::new(AtomicU64::new(0)),
        }
    }

    pub fn bump_generation(&self) -> u64 {
        self.generation.fetch_add(1, Ordering::SeqCst) + 1
    }

    pub fn prefetch_paths<T: 'static>(&self, paths: Vec<PathBuf>, cx: &mut Context<T>) {
        let current_gen = self.bump_generation();
        let image_cache = self.image_cache.clone();
        let thumb_cache = self.thumb_cache.clone();
        let in_flight = self.in_flight.clone();
        let generation = self.generation.clone();

        let filtered_paths: Vec<PathBuf> = {
            let mut guard = in_flight.lock();
            paths
                .into_iter()
                .filter(|p| !image_cache.contains(p) && guard.insert(p.clone()))
                .collect()
        };

        if filtered_paths.is_empty() {
            return;
        }

        cx.spawn(async move |this, cx| {
            let to_process = filtered_paths.clone();
            let results: Vec<_> = cx
                .background_executor()
                .spawn(async move {
                    to_process
                        .par_iter()
                        .filter_map(|path| {
                            if generation.load(Ordering::SeqCst) != current_gen {
                                return None;
                            }
                            load_image(path).ok().map(|img| {
                                let thumb = create_thumbnail(&img, 256);
                                (path.clone(), img, thumb)
                            })
                        })
                        .collect()
                })
                .await;

            let mut in_flight_guard = in_flight.lock();
            for path in &filtered_paths {
                in_flight_guard.remove(path);
            }
            for (path, img, thumb) in results {
                if let Some(t) = thumb {
                    let bytes = calc_image_bytes(&t);
                    thumb_cache.put(path.clone(), t, bytes);
                }
                let bytes = calc_image_bytes(&img);
                image_cache.put(path, img, bytes);
            }

            if let Some(this) = this.upgrade() {
                this.update(cx, |_, cx| {
                    cx.notify();
                });
            }
        })
        .detach();
    }

    pub fn prefetch_thumbnails<T: 'static>(&self, paths: Vec<PathBuf>, cx: &mut Context<T>) {
        let thumb_cache = self.thumb_cache.clone();
        let in_flight = self.in_flight.clone();

        let filtered_paths: Vec<PathBuf> = {
            let mut guard = in_flight.lock();
            paths
                .into_iter()
                .filter(|p| !thumb_cache.contains(p) && guard.insert(p.clone()))
                .collect()
        };

        if filtered_paths.is_empty() {
            return;
        }

        cx.spawn(async move |this, cx| {
            let to_process = filtered_paths.clone();
            let results: Vec<_> = cx
                .background_executor()
                .spawn(async move {
                    to_process
                        .par_iter()
                        .filter_map(|path| {
                            load_image(path).ok().and_then(|img| {
                                create_thumbnail(&img, 256).map(|thumb| (path.clone(), thumb))
                            })
                        })
                        .collect()
                })
                .await;

            let mut in_flight_guard = in_flight.lock();
            for path in &filtered_paths {
                in_flight_guard.remove(path);
            }
            for (path, thumb) in results {
                let bytes = calc_image_bytes(&thumb);
                thumb_cache.put(path, thumb, bytes);
            }

            if let Some(this) = this.upgrade() {
                this.update(cx, |_, cx| {
                    cx.notify();
                });
            }
        })
        .detach();
    }
}
