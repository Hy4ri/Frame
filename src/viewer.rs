use crate::cache::ImageCache;
use crate::loader::load_image;
use crate::rotate::{rotate_render_image, Rotation};
use gpui::{point, px, Bounds, Corners, Pixels, Point, RenderImage, Size, Window};
use std::path::{Path, PathBuf};
use std::sync::Arc;
use std::time::Instant;

pub struct ViewerState {
    pub scale: f32,
    pub offset: Point<Pixels>,
    pub rotation: Rotation,
    pub viewport_size: Size<Pixels>,

    pub current_path: Option<PathBuf>,
    pub base_image: Option<Arc<RenderImage>>,
    pub display_image: Option<Arc<RenderImage>>,
    pub is_thumbnail: bool,

    pub needs_fit: bool,
    pub is_animated: bool,
    pub anim_frame: usize,
    pub anim_last_tick: Instant,

    pub is_dragging: bool,
    pub drag_start_mouse: Point<Pixels>,
    pub drag_start_offset: Point<Pixels>,
}

impl Default for ViewerState {
    fn default() -> Self {
        Self {
            scale: 1.0,
            offset: point(px(0.0), px(0.0)),
            rotation: Rotation::R0,
            viewport_size: Size::default(),
            current_path: None,
            base_image: None,
            display_image: None,
            is_thumbnail: false,
            needs_fit: true,
            is_animated: false,
            anim_frame: 0,
            anim_last_tick: Instant::now(),
            is_dragging: false,
            drag_start_mouse: point(px(0.0), px(0.0)),
            drag_start_offset: point(px(0.0), px(0.0)),
        }
    }
}

impl ViewerState {
    pub fn load_path(&mut self, path: &Path, image_cache: &ImageCache, thumb_cache: &ImageCache) {
        self.current_path = Some(path.to_path_buf());
        self.rotation = Rotation::R0;
        self.scale = 1.0;
        self.offset = point(px(0.0), px(0.0));
        self.needs_fit = true;
        self.anim_frame = 0;
        self.anim_last_tick = Instant::now();

        if let Some(cached) = image_cache.get(path) {
            self.set_image(cached, false);
            return;
        }

        if let Some(thumb) = thumb_cache.get(path) {
            self.set_image(thumb, true);
            return;
        }

        if let Ok(loaded) = load_image(path) {
            let size = loaded.size(0);
            let byte_size = (size.width.0 * size.height.0 * 4) as usize;
            image_cache.put(path.to_path_buf(), loaded.clone(), byte_size);
            self.set_image(loaded, false);
        } else {
            self.base_image = None;
            self.display_image = None;
            self.is_thumbnail = false;
            self.is_animated = false;
        }
    }

    pub fn set_image(&mut self, img: Arc<RenderImage>, is_thumb: bool) {
        self.is_animated = img.frame_count() > 1;
        self.base_image = Some(img.clone());
        self.is_thumbnail = is_thumb;
        self.update_display_image();
    }

    fn update_display_image(&mut self) {
        if let Some(ref base) = self.base_image {
            if self.rotation == Rotation::R0 {
                self.display_image = Some(base.clone());
            } else if let Some(rotated) = rotate_render_image(base, self.rotation) {
                self.display_image = Some(rotated);
            } else {
                self.display_image = Some(base.clone());
            }
        } else {
            self.display_image = None;
        }
    }

    pub fn check_thumbnail_upgrade(&mut self, image_cache: &ImageCache) -> bool {
        if self.is_thumbnail {
            if let Some(ref path) = self.current_path {
                if let Some(full) = image_cache.get(path) {
                    self.set_image(full, false);
                    if self.viewport_size.width > px(0.0) && self.viewport_size.height > px(0.0) {
                        self.zoom_fit();
                    }
                    return true;
                }
            }
        }
        false
    }

    pub fn tick_animation(&mut self) -> bool {
        if !self.is_animated {
            return false;
        }

        if let Some(ref img) = self.display_image {
            let count = img.frame_count();
            if count <= 1 {
                return false;
            }

            let delay = img.delay(self.anim_frame);
            let delay_ms = (delay.numer_denom_ms().0 / delay.numer_denom_ms().1.max(1)).max(10);

            if self.anim_last_tick.elapsed().as_millis() >= delay_ms as u128 {
                self.anim_frame = (self.anim_frame + 1) % count;
                self.anim_last_tick = Instant::now();
                return true;
            }
        }
        false
    }

    pub fn zoom_in(&mut self) {
        self.zoom_from_center(1.05);
    }

    pub fn zoom_out(&mut self) {
        self.zoom_from_center(1.0 / 1.05);
    }

    fn zoom_from_center(&mut self, factor: f32) {
        let new_scale = (self.scale * factor).clamp(0.1, 10.0);
        let cx = self.viewport_size.width / 2.0;
        let cy = self.viewport_size.height / 2.0;

        let img_x = (cx - self.offset.x) / self.scale;
        let img_y = (cy - self.offset.y) / self.scale;

        self.offset.x = cx - (img_x * new_scale);
        self.offset.y = cy - (img_y * new_scale);
        self.scale = new_scale;
    }

    pub fn scroll_zoom(&mut self, mouse: Point<Pixels>, delta: f32) {
        let factor = (1.0 + delta * 0.01).clamp(0.5, 2.0);
        let new_scale = (self.scale * factor).clamp(0.1, 10.0);

        let img_x = (mouse.x - self.offset.x) / self.scale;
        let img_y = (mouse.y - self.offset.y) / self.scale;

        self.offset.x = mouse.x - (img_x * new_scale);
        self.offset.y = mouse.y - (img_y * new_scale);
        self.scale = new_scale;
    }

    pub fn zoom_fit(&mut self) {
        if let Some(ref img) = self.display_image {
            let size = img.size(self.anim_frame);
            let img_w = size.width.0 as f32;
            let img_h = size.height.0 as f32;

            if img_w <= 0.0 || img_h <= 0.0 {
                return;
            }

            let vp_w: f32 = self.viewport_size.width.into();
            let vp_h: f32 = self.viewport_size.height.into();

            if vp_w <= 0.0 || vp_h <= 0.0 {
                return;
            }

            let scale_w = vp_w / img_w;
            let scale_h = vp_h / img_h;
            self.scale = scale_w.min(scale_h);

            self.offset.x = px((vp_w - img_w * self.scale) / 2.0);
            self.offset.y = px((vp_h - img_h * self.scale) / 2.0);
        }
    }

    pub fn zoom_original(&mut self) {
        if let Some(ref img) = self.display_image {
            let size = img.size(self.anim_frame);
            let img_w = size.width.0 as f32;
            let img_h = size.height.0 as f32;

            self.scale = 1.0;
            let vp_w: f32 = self.viewport_size.width.into();
            let vp_h: f32 = self.viewport_size.height.into();

            self.offset.x = if vp_w > img_w {
                px((vp_w - img_w) / 2.0)
            } else {
                px(0.0)
            };
            self.offset.y = if vp_h > img_h {
                px((vp_h - img_h) / 2.0)
            } else {
                px(0.0)
            };
        }
    }

    pub fn rotate(&mut self, cw: bool) {
        self.rotation = if cw {
            self.rotation.next_cw()
        } else {
            self.rotation.next_ccw()
        };
        self.update_display_image();
        self.zoom_fit();
    }

    pub fn begin_drag(&mut self, mouse: Point<Pixels>) {
        self.is_dragging = true;
        self.drag_start_mouse = mouse;
        self.drag_start_offset = self.offset;
    }

    pub fn do_drag(&mut self, mouse: Point<Pixels>) {
        if self.is_dragging {
            let dx = mouse.x - self.drag_start_mouse.x;
            let dy = mouse.y - self.drag_start_mouse.y;
            self.offset = point(self.drag_start_offset.x + dx, self.drag_start_offset.y + dy);
        }
    }

    pub fn end_drag(&mut self) {
        self.is_dragging = false;
    }

    pub fn current_dimensions(&self) -> Option<(u32, u32)> {
        self.display_image
            .as_ref()
            .map(|img| (img.size(0).width.0 as u32, img.size(0).height.0 as u32))
    }

    pub fn paint(&mut self, bounds: Bounds<Pixels>, window: &mut Window) {
        let changed = self.viewport_size != bounds.size;
        self.viewport_size = bounds.size;

        if (self.needs_fit || changed) && self.display_image.is_some() {
            self.zoom_fit();
            self.needs_fit = false;
        }

        if let Some(ref img) = self.display_image {
            let img_size = img.size(self.anim_frame);
            let w = px(img_size.width.0 as f32 * self.scale);
            let h = px(img_size.height.0 as f32 * self.scale);

            let dest_bounds = Bounds {
                origin: point(bounds.origin.x + self.offset.x, bounds.origin.y + self.offset.y),
                size: Size {
                    width: w,
                    height: h,
                },
            };

            let _ = window.paint_image(
                dest_bounds,
                dest_bounds,
                Corners::default(),
                img.clone(),
                self.anim_frame,
                false,
            );
        }
    }
}
