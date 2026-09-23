use crate::cache::ImageCache;
use crate::loader::{create_thumbnail, load_image};
use gpui::{AppContext, Context};
use rayon::prelude::*;
use std::path::PathBuf;
use std::sync::Arc;

pub struct Prefetcher {
    image_cache: Arc<ImageCache>,
    thumb_cache: Arc<ImageCache>,
}

impl Prefetcher {
    pub fn new(image_cache: Arc<ImageCache>, thumb_cache: Arc<ImageCache>) -> Self {
        Self {
            image_cache,
            thumb_cache,
        }
    }

    pub fn prefetch_paths<T: 'static>(&self, paths: Vec<PathBuf>, cx: &mut Context<T>) {
        let image_cache = self.image_cache.clone();
        let thumb_cache = self.thumb_cache.clone();

        cx.background_spawn(async move {
            let results: Vec<_> = paths
                .par_iter()
                .filter(|p| image_cache.get(p).is_none())
                .filter_map(|path| {
                    load_image(path).ok().map(|img| {
                        let thumb = create_thumbnail(&img, 256);
                        (path.clone(), img, thumb)
                    })
                })
                .collect();

            for (path, img, thumb) in results {
                if let Some(t) = thumb {
                    let size = t.size(0);
                    let bytes = (size.width.0 * size.height.0 * 4) as usize;
                    thumb_cache.put(path.clone(), t, bytes);
                }
                let size = img.size(0);
                let bytes = (size.width.0 * size.height.0 * 4) as usize;
                image_cache.put(path, img, bytes);
            }
        })
        .detach();
    }
}
